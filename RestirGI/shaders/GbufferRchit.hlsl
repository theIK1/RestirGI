
#include "math.hlsl"
#include "samplers.hlsl"
#include "mesh.hlsl"
#include "pack_unpack.hlsl"
#include "frame_constants.hlsl"
#include "bindless_textures.hlsl"
#include "rt.hlsl"
#include "BVH.hlsl"

StructuredBuffer<MeshMaterial> MeshMaterialBuffer : register(t0, space2);

#define MY_TEXTURE_2D_BINDLESS_TABLE_SIZE 200

struct RayHitAttrib
{
    float2 bary;

};

float twice_triangle_area(float3 p0, float3 p1, float3 p2)
{
    return length(cross(p1 - p0, p2 - p0));
}

float twice_uv_area(float2 t0, float2 t1, float2 t2)
{
    return abs((t1.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (t1.y - t0.y));
}

struct BindlessTextureWithLod
{
    Texture2D tex;
    float lod;
};

// https://media.contentapi.ea.com/content/dam/ea/seed/presentations/2019-ray-tracing-gems-chapter-20-akenine-moller-et-al.pdf
BindlessTextureWithLod compute_texture_lod(uint bindless_texture_idx, float triangle_constant, float3 ray_direction, float3 surf_normal, float cone_width)
{
    float width, height;
    bindless_textures[NonUniformResourceIndex(bindless_texture_idx)].GetDimensions(width, height);
    

    float lambda = triangle_constant;
    lambda += log2(abs(cone_width));
    lambda += 0.5 * log2(width * height);

    // TODO: This blurs a lot at grazing angles; do aniso.
    lambda -= log2(abs(dot(normalize(ray_direction), surf_normal)));

    BindlessTextureWithLod res;
    res.tex = bindless_textures[NonUniformResourceIndex(bindless_texture_idx)];
    res.lod = lambda;
    return res;
}

struct STriVertex
{
    float3 position;
    float3 normal;
    float2 texCoord;
    uint meshID;
    float3 tangent;
    float3 bitangent;
    float4 color;
    uint materialID;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);

[shader("closesthit")]void GbufferRchit(inout GbufferRayPayload payload, RayHitAttrib attrib)//PackedPayload
{
    


    float3 hit_point = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    const float hit_dist = length(hit_point - WorldRayOrigin());

    float3 barycentrics = float3(1.0 - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    uint vertId = 3 * PrimitiveIndex();
    
    STriVertex v0 = BTriVertex[indices[vertId + 0]];
    STriVertex v1 = BTriVertex[indices[vertId + 1]];
    STriVertex v2 = BTriVertex[indices[vertId + 2]];
    
    float3 normal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    
    const float3 surf_normal = normalize(cross(v1.position - v0.position, v2.position - v0.position));
    
    
    float2 hitTexCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
    
    const float cone_width = payload.ray_cone.width_at_t(hit_dist);
    const float3 v0_pos_ws = mul(ObjectToWorld3x4(), float4(v0.position, 1.0));
    const float3 v1_pos_ws = mul(ObjectToWorld3x4(), float4(v1.position, 1.0));
    const float3 v2_pos_ws = mul(ObjectToWorld3x4(), float4(v2.position, 1.0));
    const float lod_triangle_constant = 0.5 * log2(twice_uv_area(v0.texCoord, v1.texCoord, v2.texCoord) / twice_triangle_area(v0_pos_ws, v1_pos_ws, v2_pos_ws));
    

    uint mesh_id = BTriVertex[indices[vertId + 0]].meshID;
    
    float3 albedo;
    float4 metalness_roughness;
    if (MeshMaterialBuffer[mesh_id].albedo_map < MY_TEXTURE_2D_BINDLESS_TABLE_SIZE)
    {
       // albedo = bindless_textures[NonUniformResourceIndex(MeshMaterialBuffer[mesh_id].albedo_map)].SampleLevel(gsamLinear, hitTexCoord, 0.0).xyz;
        
            const BindlessTextureWithLod albedo_tex =
        compute_texture_lod(MeshMaterialBuffer[mesh_id].albedo_map, lod_triangle_constant, WorldRayDirection(), surf_normal, cone_width);
        albedo = albedo_tex.tex.SampleLevel(sampler_llr, hitTexCoord, albedo_tex.lod).xyz;

        
    }
    else
        albedo = float3(1.0, 1.0, 1.0);
    if (MeshMaterialBuffer[mesh_id].spec_map < MY_TEXTURE_2D_BINDLESS_TABLE_SIZE)
    {
       // metalness_roughness = bindless_textures[NonUniformResourceIndex(MeshMaterialBuffer[mesh_id].spec_map)].SampleLevel(gsamLinear, hitTexCoord, 0.0);
        
            const BindlessTextureWithLod spec_tex =
        compute_texture_lod(MeshMaterialBuffer[mesh_id].spec_map, lod_triangle_constant, WorldRayDirection(), surf_normal, cone_width);
        metalness_roughness = spec_tex.tex.SampleLevel(sampler_llr, hitTexCoord, spec_tex.lod);
        
    }
    else
        metalness_roughness = float4(1.0, 0.0, 0.5, 1.0);
    
    float perceptual_roughness = MeshMaterialBuffer[mesh_id].roughness_mult * metalness_roughness.x;
    float roughness = clamp(perceptual_roughness_to_roughness(perceptual_roughness), 1e-4, 1.0);
    float metalness = metalness_roughness.y * MeshMaterialBuffer[mesh_id].metalness_factor;

    if (frame_constants.render_overrides.material_roughness_scale <= 1)
    {
        roughness *= frame_constants.render_overrides.material_roughness_scale;
    }
    else
    {
        roughness = square(lerp(sqrt(roughness), 1.0, 1.0 - 1.0 / frame_constants.render_overrides.material_roughness_scale));
    }
    

    float3 emissive = float3(0,0,0);
    
    GbufferData gbuffer = GbufferData::create_zero();
    gbuffer.albedo = albedo;
    gbuffer.normal = normalize(mul(ObjectToWorld3x4(), float4(normal, 0.0)));
    gbuffer.roughness = roughness;
    gbuffer.metalness = metalness;
    gbuffer.emissive = emissive;
    
        // Force double-sided
    if (dot(WorldRayDirection(), gbuffer.normal) > 0)
    {
        gbuffer.normal *= -1;
    }
    
    payload.gbuffer_packed = gbuffer.pack();
    payload.t = RayTCurrent();
    
    return;
}


#include "uv.hlsl"
#include "BVH.hlsl"
#include "frame_constants.hlsl"
#include "mesh.hlsl"
#include "LumenCommon.hlsl"

#include "gbuffer.hlsl"
#include "brdf.hlsl"
#include "brdf_lut.hlsl"
#include "layered_brdf.hlsl"
#include "blue_noise.hlsl"
#include "rt.hlsl"
#include "atmosphere.hlsl"
#include "sun.hlsl"
#include "triangle.hlsl"
#include "reservoir.hlsl"
#include "ircache_constants.hlsl"


#include "rtr_settings.hlsl" // for rtr_encode_cos_theta_for_fp16. consider moving out.
#include "rtdgi_restir_settings.hlsl"
#include "near_field_settings.hlsl"

ConstantBuffer<IndirectCB> indirectCB : register(b1);

StructuredBuffer<MeshMaterial> MeshMaterialBuffer : register(t0, space2);
Texture2D<float4> reprojected_gi_tex : register(t2);

Texture2D<float4> probe_vertex_packed : register(t3);
Texture2D<float3> geometric_normal_tex : register(t4);
Texture2D<float> depth_tex : register(t5);
StructuredBuffer<uint> AdaptiveScreenProbeData : register(t6);
StructuredBuffer<uint> NumAdaptiveScreenProbes : register(t7);
StructuredBuffer<VertexPacked> ircache_spatial_buf : register(t8);
StructuredBuffer<float4> ircache_irradiance_buf : register(t9);
TextureCube<float4> sky_cube_tex : register(t10);

RWByteAddressBuffer ircache_grid_meta_buf : register(u0, space1);

RWTexture2D<float4> probe_radiance : register(u0);
RWTexture2D<uint2> probe_reservoir : register(u1);
RWTexture2D<float4> RWtest : register(u2);
RWTexture2D<float4> probe_pre_direction : register(u3);
RWTexture2D<float> rt_history_invalidity_tex : register(u4);
RWByteAddressBuffer ircache_meta_buf : register(u5);
RWStructuredBuffer<uint> ircache_pool_buf : register(u6);
RWStructuredBuffer<VertexPacked> ircache_reposition_proposal_buf : register(u7);
RWStructuredBuffer<uint> ircache_reposition_proposal_count_buf : register(u8);
RWStructuredBuffer<uint> ircache_entry_cell_buf : register(u9);
RWByteAddressBuffer ircache_life_buf : register(u10);

#include "lookup.hlsl"

static const float4 gbuffer_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);

#define SAMPLER_SEQUENCE_LENGTH 1024

struct TraceResult
{
    float3 out_value;
    float3 hit_normal_ws;
    float hit_t;
    float pdf;
    bool is_hit;
    
};

static const float SKY_DIST = 1e4;

float3 sample_environment_light(float3 dir)
{

    return sky_cube_tex.SampleLevel(sampler_llr, dir, 0).rgb;


}

TraceResult traceRay(uint2 px, float3 normal_ws, inout uint rng, RayDesc outgoing_ray)
{
    TraceResult result;

    float3 total_radiance = 0.0.xxx;
    float3 hit_normal_ws = -outgoing_ray.Direction;
    
    float hit_t = outgoing_ray.TMax;
    
    float pdf = max(0.0, 1.0 / (dot(normal_ws, outgoing_ray.Direction) * 2 * M_PI));

    const float reflected_cone_spread_angle = 0.03;
    const RayCone ray_cone =
        pixel_ray_cone_from_image_height(gbuffer_tex_size.y * 0.5)
        .propagate(reflected_cone_spread_angle, length(outgoing_ray.Origin - get_eye_position()));
    
    const GbufferPathVertex primary_hit = GbufferRaytrace::with_ray(outgoing_ray)
        .with_cone(ray_cone)
        .with_cull_back_faces(false)
        .with_path_length(1)
        .trace(acceleration_structure);

    
    
    if (primary_hit.is_hit)
    {
        
        hit_t = primary_hit.ray_t;
        GbufferData gbuffer = unpack(primary_hit.gbuffer_packed);
        hit_normal_ws = gbuffer.normal;
        

        // Project the sample into clip space, and check if it's on-screen
        const float3 primary_hit_cs = position_world_to_sample(primary_hit.position);
        const float2 primary_hit_uv = cs_to_uv(primary_hit_cs.xy);
        const float primary_hit_screen_depth = depth_tex.SampleLevel(sampler_nnc, primary_hit_uv, 0);
        //const GbufferDataPacked primary_hit_screen_gbuffer = GbufferDataPacked::from_uint4(asuint(gbuffer_tex[int2(primary_hit_uv * gbuffer_tex_size.xy)]));
        //const float3 primary_hit_screen_normal_ws = primary_hit_screen_gbuffer.unpack_normal();
        bool is_on_screen = true
            && all(abs(primary_hit_cs.xy) < 1.0)
            && inverse_depth_relative_diff(primary_hit_cs.z, primary_hit_screen_depth) < 5e-3
            // TODO
            //&& dot(primary_hit_screen_normal_ws, -outgoing_ray.Direction) > 0.0
            //&& dot(primary_hit_screen_normal_ws, gbuffer.normal) > 0.7
        ;
        
        // If it is on-screen, we'll try to use its reprojected radiance from the previous frame
        float4 reprojected_radiance = 0;

        if (is_on_screen)
        {
            reprojected_radiance =
                reprojected_gi_tex.SampleLevel(sampler_nnc, primary_hit_uv, 0)
                * frame_constants.pre_exposure_delta;

            // Check if the temporal reprojection is valid.
            is_on_screen = reprojected_radiance.w > 0;
        }
        

        gbuffer.roughness = lerp(gbuffer.roughness, 1.0, 0.5); //ROUGHNESS_BIAS
        const float3x3 tangent_to_world = build_orthonormal_basis(gbuffer.normal); //根据单位法向量构建了一个坐标系
        const float3 wo = mul(-outgoing_ray.Direction, tangent_to_world); //将wo方向转成命中表面坐标系的向量
        const LayeredBrdf brdf = LayeredBrdf::from_gbuffer_ndotv(gbuffer, wo.z);


        
        // Sun
        float3 sun_radiance = SUN_COLOR;
        if (any(sun_radiance) > 0)
        {
            const float3 to_light_norm = sample_sun_direction(
                blue_noise_for_pixel(px, rng).xy,
                0 //USE_SOFT_SHADOWS
            );

            const bool is_shadowed =
                rt_is_shadowed(
                    acceleration_structure,
                    new_ray(
                        primary_hit.position,
                        to_light_norm,
                        1e-4,
                        SKY_DIST
                ));

            
            const float3 wi = mul(to_light_norm, tangent_to_world);
            const float3 brdf_value = brdf.evaluate(wo, wi) * max(0.0, wi.z);
            const float3 light_radiance = is_shadowed ? 0.0 : sun_radiance;
            total_radiance += brdf_value * light_radiance ;
            
    

        }
        
        if (is_on_screen)
        {
            total_radiance += reprojected_radiance.rgb * gbuffer.albedo;
        }
        else
        {
            const float3 gi = IrcacheLookupParams::create(
                    outgoing_ray.Origin,
                    primary_hit.position,
                    gbuffer.normal)
                    .with_query_rank(1)
                    .lookup(rng);
            if ((frame_constants.size.pad0 & (1 << 1)) == (1 << 1))
            {
                total_radiance += gi * gbuffer.albedo;
            }
        }

    }
    else
    {

        total_radiance += sample_environment_light(outgoing_ray.Direction);
            
    }
    
    result.out_value = total_radiance; //out_value
    
    result.hit_t = hit_t;
    result.hit_normal_ws = hit_normal_ws;
    result.pdf = pdf;
    result.is_hit = primary_hit.is_hit;
    return result;
}



[shader("raygeneration")]void LumenVaildateRgen()
{
    if (!is_rtdgi_validation_frame())
        return;
    
    uint probeIndex = DispatchRaysIndex().x;
    uint rayIndex = DispatchRaysIndex().y;
    

    
 
        // 根据行主序排列 (index % 120, index / 120)   
        /*
                       30              120
                ------------------------
                -
         20     -      P    <--  Coord = (30,20) ,  Index = 19 * 120 + 30
                -
                -
        68+34   ------------------------
        
        */
    uint2 ScreenProbeAtlasCoord = uint2(probeIndex % ScreenProbeViewSize.x, probeIndex / ScreenProbeViewSize.x);
    uint2 ScreenJitter = GetScreenTileJitter(SCREEN_TEMPORAL_INDEX);
        
    uint2 ScreenProbeScreenPosition = min((uint2) (ScreenProbeAtlasCoord * ScreenProbeDownsampleFactor + ScreenJitter), (uint2) (ViewRectMin.xy + ViewSizeAndInvSize.xy) - 1);
    
    if (probeIndex >= NumUniformScreenProbes)
    {
        int index = probeIndex;
        ScreenProbeScreenPosition = DecodeScreenProbeData(AdaptiveScreenProbeData[index]);

    }  
    
    
        
    uint2 oct_uv = uint2(rayIndex % 8, rayIndex / 8);
    uint2 TracingCoord = ScreenProbeAtlasCoord * 8 + oct_uv;
      
    uint rng = asuint(probe_pre_direction[TracingCoord].w); //uint rng = hash1(hash1(probeIndex) + frame_constants.frame_index);
        //超出自适应探针范围
    if (probeIndex >= NumUniformScreenProbes + NumAdaptiveScreenProbes[0])
    {
        return;
    }
    
    Vertex prev_probe = unpack_vertex(VertexPacked::from_float4(probe_vertex_packed[TracingCoord]));
    
    const float2 urand = r2_sequence(rng % SAMPLER_SEQUENCE_LENGTH); //0-1随机值
    float3 outgoing_dir = octa_decode((float2(oct_uv) + urand) / 8.0); //这里取值8.0和probe radiance分辨率相同
        
    RayDesc outgoing_ray;
    outgoing_ray.Direction = probe_pre_direction[TracingCoord].xyz;
    outgoing_ray.Origin = prev_probe.position;
    outgoing_ray.TMin = 0;
    outgoing_ray.TMax = SKY_DIST;

        
    TraceResult result = traceRay(ScreenProbeScreenPosition, prev_probe.normal, rng, outgoing_ray);
        
    float4 prev_value_and_count = probe_radiance[TracingCoord]
        * float4((frame_constants.pre_exposure_delta).xxx, 1);


    float invalidity = 0;

    const float3 a = result.out_value;
    const float3 b = prev_value_and_count.rgb;
    const float3 dist3 = abs(a - b) / (a + b);
    const float dist = max(dist3.r, max(dist3.g, dist3.b));
    invalidity = smoothstep(0.1, 0.5, dist);
    
     {
   
        Reservoir1spp r = Reservoir1spp::from_raw(probe_reservoir[TracingCoord]);
        
        if (r.M > 0) // 默认开启水库
        {

            r.M = max(0, min(r.M, exp2(log2(float(IRCACHE_RESTIR_M_CLAMP)) * (1.0 - invalidity))));
            

            prev_value_and_count.rgb = a;
            
            probe_reservoir[TracingCoord] = r.as_raw();
            probe_radiance[TracingCoord] = prev_value_and_count;
    
        }
    }
    //RWtest[TracingCoord] = float4(abs(a - b), 0);
    rt_history_invalidity_tex[TracingCoord] = 0;
     
 

    
    
       
}

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


#include "rtr_settings.hlsl" // for rtr_encode_cos_theta_for_fp16. consider moving out.
#include "rtdgi_restir_settings.hlsl"
#include "near_field_settings.hlsl"

ConstantBuffer<IndirectCB> indirectCB : register(b1);

StructuredBuffer<MeshMaterial> MeshMaterialBuffer : register(t0, space2);

Texture2D<float4> reprojected_gi_tex : register(t2);
Texture2D<float> ScreenProbeSceneDepth : register(t3);
Texture2D<float3> geometric_normal_tex : register(t4);
Texture2D<float> depth_tex : register(t5);
StructuredBuffer<uint> AdaptiveScreenProbeData : register(t6);
StructuredBuffer<uint> NumAdaptiveScreenProbes : register(t7);
StructuredBuffer<VertexPacked> ircache_spatial_buf : register(t8);
StructuredBuffer<float4> ircache_irradiance_buf : register(t9);
TextureCube<float4> sky_cube_tex : register(t10);

RWByteAddressBuffer ircache_grid_meta_buf : register(u0, space1);

RWTexture2D<float4> probe_radiance : register(u0);
RWTexture2D<float> probe_hit_radiance : register(u1);
RWTexture2D<uint2> probe_reservoir : register(u2);
RWTexture2D<float4> probe_vertex_packed : register(u3);
RWTexture2D<float4> probe_pre_direction : register(u4);
RWTexture2D<float> ScreenProbeSceneHistoryDepth : register(u5);
RWByteAddressBuffer ircache_meta_buf : register(u6);
RWStructuredBuffer<uint> ircache_pool_buf : register(u7);
RWStructuredBuffer<VertexPacked> ircache_reposition_proposal_buf : register(u8);
RWStructuredBuffer<uint> ircache_reposition_proposal_count_buf : register(u9);
RWStructuredBuffer<uint> ircache_entry_cell_buf : register(u10);
RWByteAddressBuffer ircache_life_buf : register(u11);
RWTexture2D<float4> RWtest : register(u12);

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
            total_radiance += brdf_value * light_radiance;
            
    

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



[shader("raygeneration")]void LumenRayGen()
{
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
        
        const float2 uv = get_uv(ScreenProbeScreenPosition, gbuffer_tex_size);
        float depth = ScreenProbeSceneDepth[ScreenProbeAtlasCoord];
        float history_depth = ScreenProbeSceneHistoryDepth[ScreenProbeAtlasCoord];
        float depth_diff = inverse_depth_relative_diff(depth,history_depth); // abs(depth / history_depth - 1)
        const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, depth);
        
        uint rng = hash1(hash1(probeIndex) + frame_constants.frame_index);
        
        const float3 normal_vs = geometric_normal_tex[ScreenProbeScreenPosition];
        const float3 normal_ws = direction_view_to_world(normal_vs);
        
        uint2 oct_uv = uint2(rayIndex % 8, rayIndex / 8);
        uint2 TracingCoord = ScreenProbeAtlasCoord * 8 + oct_uv;
        
        //超出自适应探针范围
        if (probeIndex >= NumUniformScreenProbes + NumAdaptiveScreenProbes[0])
        {
            probe_radiance[TracingCoord] = float4(0, 0, 0, 0);
            probe_hit_radiance[TracingCoord] = 0;
            return;
        }
            
    
         float2 urand = r2_sequence(rng % SAMPLER_SEQUENCE_LENGTH); //0-1随机值
        
        float3 outgoing_dir = octa_decode((float2(oct_uv) + urand) / 8.0); //这里取值8.0和probe radiance分辨率相同
        
        RayDesc outgoing_ray;
        outgoing_ray.Direction = outgoing_dir;
        outgoing_ray.Origin = view_ray_context.biased_secondary_ray_origin_ws_with_normal(normal_ws); //view_ray_context.ray_hit_ws() 
        outgoing_ray.TMin = 0;
        outgoing_ray.TMax = SKY_DIST;

        
        TraceResult result = traceRay(ScreenProbeScreenPosition, normal_ws, rng, outgoing_ray);
    
        const float3 new_value = result.out_value;
        const float new_lum = sRGB_to_luminance(new_value);

        Reservoir1sppStreamState stream_state = Reservoir1sppStreamState::create();
        Reservoir1spp reservoir = Reservoir1spp::create();
        const uint reservoir_payload = TracingCoord.x | (TracingCoord.y << 16); //record pixel position
        reservoir.init_with_stream(new_lum, 1.0, stream_state, reservoir_payload);
        
        float4 prev_value_and_count = probe_radiance[TracingCoord]
        * float4((frame_constants.pre_exposure_delta).xxx, 1);

        float3 val_sel = new_value;
        bool selected_new = true;
        
        const uint M_CLAMP = 30;
        Reservoir1spp r = Reservoir1spp::from_raw(probe_reservoir[TracingCoord]);
        
    if (r.M > 0 && indirectCB.enableReservoir == 1 && depth_diff < indirectCB.depthDiff) // 默认开启水库
        {
            r.M = min(r.M, M_CLAMP);

            if (reservoir.update_with_stream(
                r, sRGB_to_luminance(prev_value_and_count.rgb), 1.0,
                stream_state, r.payload, rng
            ))
            {
                val_sel = prev_value_and_count.rgb;
                selected_new = false;
            }
        }
    

    reservoir.finish_stream(stream_state);
     
    probe_reservoir[TracingCoord] = reservoir.as_raw();
    probe_radiance[TracingCoord] = float4(val_sel, reservoir.W);
    probe_hit_radiance[TracingCoord] = length(outgoing_ray.Direction * result.hit_t);
    ScreenProbeSceneHistoryDepth[ScreenProbeAtlasCoord] = ScreenProbeSceneDepth[ScreenProbeAtlasCoord];
    if (selected_new)
    {
        Vertex vertex;
        vertex.position = outgoing_ray.Origin;
        vertex.normal = normal_ws;
        probe_vertex_packed[TracingCoord] = pack_vertex(vertex).data0;
        probe_pre_direction[TracingCoord] = float4(outgoing_ray.Direction, asfloat(rng));
        RWtest[TracingCoord] = float4(1, 0, 0, r.M);

    }
    else
        RWtest[TracingCoord] = float4(0, 0, 0, r.M);
    
    
       
}

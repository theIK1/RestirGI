#include "uv.hlsl"
#include "pack_unpack.hlsl"
#include "frame_constants.hlsl"
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
#include "mesh.hlsl"

#include "rtdgi_restir_settings.hlsl"

#include "BVH.hlsl"
StructuredBuffer<MeshMaterial> MeshMaterialBuffer : register(t0,space2);
Texture2D<float4> reprojected_gi_tex : register(t1,space1);
Texture2D<float4> reservoir_ray_history_tex : register(t2,space1);
Texture2D<float3> ray_orig_history_tex : register(t3,space1);

Texture2D<float3> half_view_normal_tex : register(t2);
Texture2D<float> depth_tex : register(t3);
Texture2D<float4> reprojection_tex : register(t4);
StructuredBuffer<VertexPacked> ircache_spatial_buf : register(t5);
StructuredBuffer<float4> ircache_irradiance_buf : register(t6);
Texture2D<float4> wrc_radiance_atlas_tex : register(t7);
TextureCube<float4> sky_cube_tex : register(t8);

RWByteAddressBuffer ircache_grid_meta_buf : register(u0,space1);
RWTexture2D<uint2> reservoir_tex : register(u1,space1);
RWTexture2D<float4> irradiance_history_tex : register(u2,space1);

RWByteAddressBuffer ircache_meta_buf : register(u0);
RWStructuredBuffer<uint> ircache_pool_buf : register(u1);
RWStructuredBuffer<VertexPacked> ircache_reposition_proposal_buf : register(u2);
RWStructuredBuffer<uint> ircache_reposition_proposal_count_buf : register(u3);
RWStructuredBuffer<uint> ircache_entry_cell_buf : register(u4);
RWByteAddressBuffer ircache_life_buf : register(u5);

RWTexture2D<float> rt_history_invalidity_out_tex : register(u6);
RWByteAddressBuffer test_buf : register(u7);
RWTexture2D<float4> RWTest : register(u8);

static const float4 gbuffer_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);
//#define IRCACHE_LOOKUP_DONT_KEEP_ALIVE
//#define IRCACHE_LOOKUP_KEEP_ALIVE_PROB 0.125

#include "lookup.hlsl"
#include "Wrclookup.hlsl"
#include "candidate_ray_dir.hlsl"

#include "diffuse_trace_common_inc.hlsl"


[shader("raygeneration")]
void RtdgiValidateRgen()
{

    
    const uint2 px = DispatchRaysIndex().xy;
    const int2 hi_px_offset = HALFRES_SUBSAMPLE_OFFSET;
    const uint2 hi_px = px * 2 + hi_px_offset;
    
    
    if (0.0 == depth_tex[hi_px])
    {
        rt_history_invalidity_out_tex[px] = 1;
        return;
    }

    float invalidity = 0.0;

    if (RESTIR_USE_PATH_VALIDATION && is_rtdgi_validation_frame())
    {
        const float3 normal_vs = half_view_normal_tex[px];
        const float3 normal_ws = direction_view_to_world(normal_vs);

        const float3 prev_ray_orig = ray_orig_history_tex[px];
        const float3 prev_hit_pos = reservoir_ray_history_tex[px].xyz + prev_ray_orig;

        const float4 prev_radiance_packed = irradiance_history_tex[px];
        const float3 prev_radiance = max(0.0.xxx, prev_radiance_packed.rgb);

        RayDesc prev_ray;
        prev_ray.Direction = normalize(prev_hit_pos - prev_ray_orig);
        prev_ray.Origin = prev_ray_orig;
        prev_ray.TMin = 0;
        prev_ray.TMax = SKY_DIST;

        // TODO: frame index
        uint rng = hash3(uint3(px, 0));
        
        TraceResult result = do_the_thing(px, normal_ws, rng, prev_ray);
        const float3 new_radiance = max(0.0.xxx, result.out_value);

        const float rad_diff = length(abs(prev_radiance - new_radiance) / max(1e-3, prev_radiance + new_radiance));
        invalidity = smoothstep(0.1, 0.5, rad_diff / length(1.0.xxx));

        const float prev_hit_dist = length(prev_hit_pos - prev_ray_orig);

        // If we hit more or less the same point, replace the hit radiance.
        // If the hit is different, it's possible that the previous origin point got obscured
        // by something, in which case we want M-clamping to take care of it instead.
        if (abs(result.hit_t - prev_hit_dist) / (prev_hit_dist + prev_hit_dist) < 0.2)
        {
            irradiance_history_tex[px] = float4(new_radiance, prev_radiance_packed.a);

            // When we update the radiance, we might end up with fairly low probability
            // rays hitting the bright spots by chance. The PDF division compounded
            // by the increase in radiance causes fireflies to appear.
            // As a HACK, we will clamp that by scaling down the `M` factor then.
            Reservoir1spp r = Reservoir1spp::from_raw(reservoir_tex[px]);
            const float lum_old = sRGB_to_luminance(prev_radiance);
            const float lum_new = sRGB_to_luminance(new_radiance);
            r.M *= clamp(lum_old / max(1e-8, lum_new), 0.03, 1.0);

            // Allow the new value to be greater than the old one up to a certain scale,
            // then dim it down by reducing `W`. It will recover over time.
            const float allowed_luminance_increment = 10.0;
            r.W *= clamp(lum_old / max(1e-8, lum_new) * allowed_luminance_increment, 0.01, 1.0);

            reservoir_tex[px] = r.as_raw();

        }
    }

    rt_history_invalidity_out_tex[px] = invalidity;
    RWTest[px] = float4(invalidity,0,0,0);

}

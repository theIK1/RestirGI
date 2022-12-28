#include "uv.hlsl"
#include "pack_unpack.hlsl"
#include "frame_constants.hlsl"
#include "gbuffer.hlsl"
#include "brdf.hlsl"
#include "brdf_lut.hlsl"
#include "layered_brdf.hlsl"
#include "rt.hlsl"
#include "hash.hlsl"
#include "quasi_random.hlsl"
#include "reservoir.hlsl"
#include "bindless_textures.hlsl"
#include "atmosphere.hlsl"
#include "mesh.hlsl"
#include "triangle.hlsl"
#include "color.hlsl"
#include "BVH.hlsl"

StructuredBuffer<VertexPacked> ircache_spatial_buf : register(t1, space1);
Texture2D<float4> wrc_radiance_atlas_tex : register(t2, space1);
StructuredBuffer<uint> ircache_entry_indirection_buf : register(t3, space1);
TextureCube<float4> sky_cube_tex : register(t4, space1);
StructuredBuffer<MeshMaterial> MeshMaterialBuffer : register(t0, space2);

RWByteAddressBuffer ircache_grid_meta_buf : register(u0);

RWByteAddressBuffer ircache_life_buf : register(u1);
RWStructuredBuffer<VertexPacked> ircache_reposition_proposal_buf : register(u2);
RWStructuredBuffer<uint> ircache_reposition_proposal_count_buf : register(u3);
RWByteAddressBuffer ircache_meta_buf : register(u4);
RWStructuredBuffer<float4> ircache_aux_buf : register(u5);
RWStructuredBuffer<uint> ircache_pool_buf : register(u6);
RWStructuredBuffer<uint> ircache_entry_cell_buf : register(u7);

#include "sun.hlsl"
#include "wrcLookup.hlsl"

// Sample straight from the `ircache_aux_buf` instead of the SH.
#define IRCACHE_LOOKUP_PRECISE
#include "lookup.hlsl"

#include "ircache_sampler_common_inc.hlsl"
#include "ircache_trace_common_inc.hlsl"

[shader("raygeneration")]
void TraceIrradianceRgen()
{
    if (IRCACHE_FREEZE) {
        return;
    }

    const uint dispatch_idx = DispatchRaysIndex().x;

    // AMD ray-tracing bug workaround; indirect RT seems to be tracing with the same
    // ray count for multiple dispatches (???)
    // Search for c804a814-fdc8-4843-b2c8-9d0674c10a6f for other occurences.
    #if 1
        const uint alloc_count = ircache_meta_buf.Load(IRCACHE_META_TRACING_ALLOC_COUNT);
        if (dispatch_idx >= alloc_count * IRCACHE_SAMPLES_PER_FRAME) {
            return;
        }
    #endif

    const uint entry_idx = ircache_entry_indirection_buf[dispatch_idx / IRCACHE_SAMPLES_PER_FRAME];
    const uint sample_idx = dispatch_idx % IRCACHE_SAMPLES_PER_FRAME;
    const uint life = ircache_life_buf.Load(entry_idx * 4);
    const uint rank = ircache_entry_life_to_rank(life);

    VertexPacked packed_entry = ircache_spatial_buf[entry_idx];
    const Vertex entry = unpack_vertex(packed_entry);

    DiffuseBrdf brdf;
    //const float3x3 tangent_to_world = build_orthonormal_basis(entry.normal);

    brdf.albedo = 1.0.xxx;

    // Allocate fewer samples for further bounces
    #if 0
        const uint sample_count_divisor = 
            rank <= 1
            ? 1
            : 4;
    #else
        const uint sample_count_divisor = 1;
    #endif

    uint rng = hash1(hash1(entry_idx) + frame_constants.frame_index);

    const SampleParams sample_params = SampleParams::from_spf_entry_sample_frame(
        IRCACHE_SAMPLES_PER_FRAME,
        entry_idx,
        sample_idx,
        frame_constants.frame_index);

    IrcacheTraceResult traced = ircache_trace(entry, brdf, sample_params, life);

    const float self_lighting_limiter = 
        USE_SELF_LIGHTING_LIMITER
        ? lerp(0.5, 1, smoothstep(-0.1, 0, dot(traced.direction, entry.normal)))
        : 1.0;

    const float3 new_value = traced.incident_radiance * self_lighting_limiter;
    const float new_lum = sRGB_to_luminance(new_value);

    Reservoir1sppStreamState stream_state = Reservoir1sppStreamState::create();
    Reservoir1spp reservoir = Reservoir1spp::create();
    reservoir.init_with_stream(new_lum, 1.0, stream_state, sample_params.raw());

    const uint octa_idx = sample_params.octa_idx();
    const uint output_idx = entry_idx * IRCACHE_AUX_STRIDE + octa_idx; //用以存储水库

    float4 prev_value_and_count =
        ircache_aux_buf[output_idx + IRCACHE_OCTA_DIMS2] 
        * float4((frame_constants.pre_exposure_delta).xxx, 1);

    float3 val_sel = new_value;
    bool selected_new = true;

    {
        const uint M_CLAMP = 30;

        Reservoir1spp r = Reservoir1spp::from_raw(asuint(ircache_aux_buf[output_idx].xy));
        if (r.M > 0) {
            r.M = min(r.M, M_CLAMP);

            Vertex prev_entry = unpack_vertex(VertexPacked::from_float4(ircache_aux_buf[output_idx + IRCACHE_OCTA_DIMS2 * 2]));
            //prev_entry.position = entry.position;

            if (reservoir.update_with_stream(
                r, sRGB_to_luminance(prev_value_and_count.rgb), 1.0,
                stream_state, r.payload, rng
            )) {
                val_sel = prev_value_and_count.rgb;
                selected_new = false;
            }
        }
    }

    reservoir.finish_stream(stream_state);

    ircache_aux_buf[output_idx].xy = asfloat(reservoir.as_raw());
    ircache_aux_buf[output_idx + IRCACHE_OCTA_DIMS2] = float4(val_sel, reservoir.W); //用以存储radiance

    if (selected_new) {
        ircache_aux_buf[output_idx + IRCACHE_OCTA_DIMS2 * 2] = packed_entry.data0;
    }
}

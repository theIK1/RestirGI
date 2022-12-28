#include "frame_constants.hlsl"


Texture2D<uint> meta_tex : register(t0);
Texture2D<float3> geometric_normal_tex : register(t1);
Texture2D<float> depth_tex : register(t2);
Texture2D<float2> input_tex : register(t3);

RWTexture2D<float2> output_tex : register(u0);

ConstantBuffer<ShadowSpatialCB> shadowSpatialCB : register(b1);

// Would be nice, but not suppored on GTX1xxx hardware
// according to https://vulkan.gpuinfo.org/listdevicescoverage.php?core=1.2&feature=shaderFloat16&platform=windows
#define float16_t2 float2
#define float16_t3 float3

uint2 FFX_DNSR_Shadows_GetBufferDimensions() {
    return uint2(shadowSpatialCB.input_tex_size.xy);
}

float2 FFX_DNSR_Shadows_GetInvBufferDimensions() {
    return shadowSpatialCB.input_tex_size.zw;
}

float4x4 FFX_DNSR_Shadows_GetProjectionInverse() {
    return frame_constants.view_constants.clip_to_view;
}

float FFX_DNSR_Shadows_GetDepthSimilaritySigma() {
    return 0.01;
}

bool FFX_DNSR_Shadows_IsShadowReciever(uint2 px) {
    return depth_tex[px] != 0.0;
}

float16_t3 FFX_DNSR_Shadows_ReadNormals(uint2 px) {
    float3 normal_vs = geometric_normal_tex[px] * 2.0 - 1.0;
    return float16_t3(normal_vs);
}

float FFX_DNSR_Shadows_ReadDepth(uint2 px) {
    return depth_tex[px];
}

float16_t2 FFX_DNSR_Shadows_ReadInput(uint2 px) {
    return float16_t2(input_tex[px]);
}

uint FFX_DNSR_Shadows_ReadTileMetaData(uint linear_tile_index) {
    const uint2 tile = uint2(
        linear_tile_index % shadowSpatialCB.bitpacked_shadow_mask_extent.x,
        linear_tile_index / shadowSpatialCB.bitpacked_shadow_mask_extent.x
    );

    return meta_tex[tile];
}

#include "ffx_denoiser_shadows_filter.hlsl"

[numthreads(8, 8, 1)]
void CS(uint2 px: SV_DispatchThreadID, uint2 gtid: SV_GroupThreadID, uint2 gid: SV_GroupID) {
    const uint pass_idx = 0;

    bool write_results = true;
    float2 output = FFX_DNSR_Shadows_FilterSoftShadowsPass(gid, gtid, px, write_results, pass_idx, shadowSpatialCB.step_size);

    if (write_results) {
        output_tex[px] = max(0.0, output);
    } else {
        //wut?
        //output_tex[px] = 0.0;
    }

    //output_tex[px] = input_tex[px];
}

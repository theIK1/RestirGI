 Texture2D<float> input_tex : register(t0);
RWTexture2D<uint> output_tex : register(u0);



static const float4 input_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);
static const uint2 bitpacked_shadow_mask_extent = uint2(240,270);

uint2 FFX_DNSR_Shadows_GetBufferDimensions() {
    return uint2(input_tex_size.xy);
}

bool FFX_DNSR_Shadows_HitsLight(uint2 px, uint2 gtid, uint2 gid) {
    return input_tex[px] > 0.5;
}

void FFX_DNSR_Shadows_WriteMask(uint linear_tile_index, uint value) {
    const uint2 tile = uint2(
        linear_tile_index % bitpacked_shadow_mask_extent.x,
        linear_tile_index / bitpacked_shadow_mask_extent.x
    );

    output_tex[tile] = value;
}

#include "ffx_denoiser_shadows_prepare.hlsl"

[numthreads(8, 4, 1)]
void CS(uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID) {
    FFX_DNSR_Shadows_PrepareShadowMask(gtid, gid);
}

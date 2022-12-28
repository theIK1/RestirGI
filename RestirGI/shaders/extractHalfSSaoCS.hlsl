#include "frame_constants.hlsl"
#include "pack_unpack.hlsl"

Texture2D<float> input_tex : register(t0);
RWTexture2D<float> output_tex : register(u0);




[numthreads(8, 8, 1)]
void CS(in int2 px : SV_DispatchThreadID) {
    const int2 src_px = px * 2 + HALFRES_SUBSAMPLE_OFFSET;

	output_tex[px] = input_tex[src_px];
}

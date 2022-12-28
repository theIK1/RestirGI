#include "frame_constants.hlsl"


Texture2D<float4> input_tex : register(t0);


RWTexture2D<float4> output_tex : register(u0);



[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    int2 px = DispatchThreadId.xy;

    const int2 src_px = px * 2 + HALFRES_SUBSAMPLE_OFFSET;

    output_tex[px] = input_tex[src_px];
   

}
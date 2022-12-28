
#include "uv.hlsl"
#include "reservoir.hlsl"


#include "../structure.h"



Texture2D<uint2> input : register(t0);


RWTexture2D<float4> RWtest : register(u0);

static const float4 output_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);




[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    uint2 px = DispatchThreadId.xy;

    float2 uv = get_uv(px, output_tex_size);
    

    Reservoir1spp r = Reservoir1spp::from_raw(input[px]);

    
   // RWtest[DispatchThreadId.xy] = float4(r.w_sum,r.M,r.W,r.payload);
    //RWtest[DispatchThreadId.xy] = float4(114, 114, 114, 114);

   
    
    
    

}
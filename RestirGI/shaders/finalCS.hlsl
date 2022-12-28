
#include "uv.hlsl"


#include "../structure.h"


Texture2D<float4> reservoir_history_tex : register(t0);
Texture2D<float4> candidate_radiance_tex : register(t1);
Texture2D<float4> invalidity_output_tex : register(t2);
Texture2D<float4> radiance_output_tex : register(t3);
Texture2D<float4> bounced_radiance_output_tex1 : register(t4);
Texture2D<float4> irradiance_output_tex : register(t5);
Texture2D<float4> temporal_filtered_tex : register(t6);
Texture2D<float4> spatial_filtered_tex : register(t7);
Texture2D<float4> debug_out_tex : register(t8);
Texture2D<float4> test : register(t9);



    

enum ShowDebug
{
		RTDGI_VALIDATE,
		RTDGI_TRACE,
		VALIDITY_INTEGRATE,
		RESTIR_TEMPORAL,
		RESTIR_SPATIAL,
		RESTIR_RESOLVE,
		RTDGI_TEMPORAL,
		RTDGI_SPATIAL,
		FINAL,
		TEST,
        DEFAULT,
};




RWTexture2D<float4> RWoutput : register(u0);

static const float4 output_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);

SamplerState gsamLinear : register(s3);

ConstantBuffer<FinalCB> finalCB : register(b0);

[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    uint2 px = DispatchThreadId.xy;

    float2 uv = get_uv(px, output_tex_size);
    
    if (finalCB.control == ShowDebug::RTDGI_VALIDATE)
    {
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 0)] = reservoir_history_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 1)] = reservoir_history_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 0)] = reservoir_history_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 1)] = reservoir_history_tex[DispatchThreadId.xy];
        return;
    }
    if (finalCB.control == ShowDebug::RTDGI_TRACE)
    {
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 0)] = candidate_radiance_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 1)] = candidate_radiance_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 0)] = candidate_radiance_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 1)] = candidate_radiance_tex[DispatchThreadId.xy];

        return;
    }
    if (finalCB.control == ShowDebug::VALIDITY_INTEGRATE)
    {
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 0)] = invalidity_output_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 1)] = invalidity_output_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 0)] = invalidity_output_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 1)] = invalidity_output_tex[DispatchThreadId.xy];
        return;
    }
    
    if (finalCB.control == ShowDebug::RESTIR_TEMPORAL)
    {
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 0)] = radiance_output_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 1)] = radiance_output_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 0)] = radiance_output_tex[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 1)] = radiance_output_tex[DispatchThreadId.xy];
        return;
    }
    if (finalCB.control == ShowDebug::RESTIR_SPATIAL)
    {
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 0)] = bounced_radiance_output_tex1[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(0, 1)] = bounced_radiance_output_tex1[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 0)] = bounced_radiance_output_tex1[DispatchThreadId.xy];
        RWoutput[DispatchThreadId.xy * 2 + uint2(1, 1)] = bounced_radiance_output_tex1[DispatchThreadId.xy];
        return;
    }
    if (finalCB.control == ShowDebug::RESTIR_RESOLVE)
    {
        RWoutput[DispatchThreadId.xy] = irradiance_output_tex[DispatchThreadId.xy];
        return;
    }
    
    if (finalCB.control == ShowDebug::RTDGI_TEMPORAL)
    {
        RWoutput[DispatchThreadId.xy] = temporal_filtered_tex[DispatchThreadId.xy];
        return;
    }
    if (finalCB.control == ShowDebug::RTDGI_SPATIAL)
    {
        RWoutput[DispatchThreadId.xy] = spatial_filtered_tex[DispatchThreadId.xy];
        return;
    }
    
    if (finalCB.control == ShowDebug::FINAL)
    {
        RWoutput[DispatchThreadId.xy] = debug_out_tex[DispatchThreadId.xy];
        return;
    }
    
    if (finalCB.control == ShowDebug::TEST)
    {
        RWoutput[DispatchThreadId.xy] = test[DispatchThreadId.xy];


        return;
    }
    
    

}
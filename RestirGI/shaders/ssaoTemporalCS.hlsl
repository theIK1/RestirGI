
#include "uv.hlsl"
#include "samplers.hlsl"
Texture2D<float4> input_tex : register(t0);
Texture2D<float4> reprojection_tex : register(t1);
Texture2D<float4> history_tex : register(t2);

RWTexture2D<float4> final_output_tex : register(u0);
RWTexture2D<float4> history_output_tex : register(u1);



static const float4 output_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);

#define USE_AO_ONLY 1

#define LINEAR_TO_WORKING(x) x
#define WORKING_TO_LINEAR(x) x


[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{

    uint2 px = DispatchThreadId.xy;

    float2 uv = get_uv(px, output_tex_size);
    
    float4 center = WORKING_TO_LINEAR(input_tex[px]);
    float4 reproj = reprojection_tex[px];
    float4 history = WORKING_TO_LINEAR(history_tex.SampleLevel(sampler_lnc, uv + reproj.xy, 0));
    
    float4 vsum = 0.0.xxxx;
    float4 vsum2 = 0.0.xxxx;
    float wsum = 0.0;

    const int k = 2;
    for (int y = -k; y <= k; ++y)
    {
        for (int x = -k; x <= k; ++x)
        {
            float4 neigh = WORKING_TO_LINEAR(input_tex[px + int2(x, y) * 2]);
            float w = exp(-3.0 * float(x * x + y * y) / float((k + 1.) * (k + 1.)));
            vsum += neigh * w;
            vsum2 += neigh * neigh * w;
            wsum += w;
        }
    }

    float4 ex = vsum / wsum;
    float4 ex2 = vsum2 / wsum;
    float4 dev = sqrt(max(0.0.xxxx, ex2 - ex * ex));

    float box_size = 0.5;

    const float n_deviations = 5.0;
    float4 nmin = lerp(center, ex, box_size * box_size) - dev * box_size * n_deviations;
    float4 nmax = lerp(center, ex, box_size * box_size) + dev * box_size * n_deviations;
    
    float4 clamped_history = clamp(history, nmin, nmax);
    //float4 res = lerp(clamped_history, center, lerp(1.0, 1.0 / 12.0, reproj.z));
    float4 res = lerp(clamped_history, center, 1.0 / 8.0);

#if USE_AO_ONLY
    res = res.r;
#endif
    
    history_output_tex[px] = LINEAR_TO_WORKING(res);
    final_output_tex[px] = res;
}
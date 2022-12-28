#include "pack_unpack.hlsl"
#include "frame_constants.hlsl"
#include "sh.hlsl"
#include "quasi_random.hlsl"
#include "reservoir.hlsl"
#include "gbuffer.hlsl"


#include "LumenCommon.hlsl"

StructuredBuffer<uint> NumAdaptiveScreenProbes : register(t0);
Texture2D<float4> gbuffer_tex : register(t1);

RWTexture2D<float4> probe_radiance : register(u0);
RWStructuredBuffer<float4> probe_sh : register(u1);
RWStructuredBuffer<float3> my_probe_sh : register(u2);

#define SAMPLER_SEQUENCE_LENGTH 1024

struct Contribution
{
    //等同于4个float3
    // float3 SH[0] = 0.282095 * radiance
     // float3 SH[1] = 0.488603 * radiance *direction.y
     // float3 SH[2] = 0.488603 * radiance * direction.z
     // float3 SH[3] = 0.488603 * radiance * direction.x
    float4 sh_rgb[3];
    // sh_rgb[0] = (0.282095,0.488603*direction.x,0.488603*direction.y,0.488603*direction.z)* radiance.r

    void add_radiance_in_direction(float3 radiance, float3 direction)
    {
        // https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/gdc2018-precomputedgiobalilluminationinfrostbite.pdf
        // `shEvaluateL1`, plus the `4` factor, with `pi` cancelled out in the evaluation code (BRDF).
        float4 sh = float4(0.282095, direction * 0.488603) * 4;
        sh_rgb[0] += sh * radiance.r;
        sh_rgb[1] += sh * radiance.g;
        sh_rgb[2] += sh * radiance.b;
    }

    void scale(float value)
    {
        sh_rgb[0] *= value;
        sh_rgb[1] *= value;
        sh_rgb[2] *= value;
    }
};

struct CalSH
{
    float3 SH[4];
    
    void add_radiance_in_direction(float3 radiance, float3 direction)
    {
        // https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/gdc2018-precomputedgiobalilluminationinfrostbite.pdf
        // `shEvaluateL1`, plus the `4` factor, with `pi` cancelled out in the evaluation code (BRDF).
        SH[0] += 0.282095 * radiance * 4 * M_PI;
        SH[1] += 0.488603 * radiance * direction.z * 4 * M_PI;
        SH[2] += 0.488603 * radiance * direction.y * 4 * M_PI;
        SH[3] += 0.488603 * radiance * direction.x * 4 * M_PI;
    }
    
    void scale(float value)
    {
        SH[0] *= value;
        SH[1] *= value;
        SH[2] *= value;
        SH[3] *= value;
    }
};


[numthreads(102, 1, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    uint probeIndex = DispatchThreadId.x;
    //超出自适应探针范围
    if (probeIndex >= NumUniformScreenProbes + NumAdaptiveScreenProbes[0])
    {
        return;
    }
    uint2 ScreenProbeAtlasCoord = uint2(probeIndex % ScreenProbeViewSize.x, probeIndex / ScreenProbeViewSize.x);
    
    GbufferData gbuffer = unpack(GbufferDataPacked::from_uint4(asuint(gbuffer_tex[ScreenProbeAtlasCoord*16])));
    Contribution contribution_sum = (Contribution) 0;
    CalSH calsh_sum = (CalSH) 0;
    
    {
        float valid_samples = 0;

        // TODO: counter distortion
        for (uint octa_idx = 0; octa_idx < 8 * 8 ; ++octa_idx)
        {
            uint2 oct_uv = uint2(octa_idx % 8, octa_idx / 8);

            uint rng = hash1(hash1(probeIndex) + frame_constants.frame_index);
            const float2 urand = r2_sequence(rng % SAMPLER_SEQUENCE_LENGTH); //0-1随机值
            float3 dir = octa_decode((float2(oct_uv) + urand) / 8.0); //这里取值8.0和probe radiance分辨率相同
            
          

            float4 contrib = probe_radiance[ScreenProbeAtlasCoord * 8 + oct_uv];
            contrib.rgb = 1.22 * contrib.rgb;

            contribution_sum.add_radiance_in_direction(
                contrib.rgb * contrib.w,
                dir
            );
            calsh_sum.add_radiance_in_direction(contrib.rgb,
                dir);
            valid_samples += contrib.w > 0 ? 1.0 : 0.0;
           

        }

        contribution_sum.scale(1.0 / max(1.0, valid_samples));
        calsh_sum.scale(1.0 / max(1.0, valid_samples));

    }

    for (uint basis_i = 0; basis_i < 3; ++basis_i)
    {
        const float4 new_value = contribution_sum.sh_rgb[basis_i];
        float4 prev_value =
                probe_sh[probeIndex * 3 + basis_i]
                * frame_constants.pre_exposure_delta;

        const bool should_reset = all(0.0 == prev_value);
        if (should_reset)
        {
            prev_value = new_value;
        }

        float blend_factor_new = 0.25;
            //float blend_factor_new = 1;
        const float4 blended_value = lerp(prev_value, new_value, blend_factor_new);

        probe_sh[probeIndex * 3 + basis_i] = blended_value;
    }
    
    for (uint i = 0; i < 4; ++i)
    {
        const float3 new_value = calsh_sum.SH[i];
        float3 prev_value =
                my_probe_sh[probeIndex * 3 + i]
                * frame_constants.pre_exposure_delta;

        const bool should_reset = all(0.0 == prev_value);
        if (should_reset)
        {
            prev_value = new_value;
        }

        float blend_factor_new = 0.25;
            //float blend_factor_new = 1;
        const float3 blended_value = lerp(prev_value, new_value, blend_factor_new);

        my_probe_sh[probeIndex * 4 + i] = blended_value;
    }
}
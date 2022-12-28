#include "gbuffer.hlsl"
#include "frame_constants.hlsl"
ConstantBuffer<IndirectCB> indirectCB : register(b1);

Texture2D<float4> gbuffer_tex : register(t0);
Texture2D<float4> probe_radiance : register(t1);
StructuredBuffer<float4> probe_sh : register(t2);
Texture2D<float3> geometric_normal_tex : register(t3);
Texture2D<float> depth_tex : register(t4);
StructuredBuffer<float3> my_probe_sh : register(t5);
Texture2D<float4> probe_pre_direction : register(t6);

RWTexture2D<float4> output_tex : register(u0);
RWTexture2D<uint> RWScreenTileAdaptiveProbeHeader : register(u1);
RWTexture2D<uint> RWScreenTileAdaptiveProbeIndices : register(u2);
RWStructuredBuffer<uint> RWAdaptiveScreenProbeData : register(u3);
RWStructuredBuffer<uint> RWNumAdaptiveScreenProbes : register(u4);
RWTexture2D<float> RWScreenProbeSceneDepth : register(u5);
RWTexture2D<float4> RWtest : register(u6);
#include "adaptiveScreenProbe.hlsl"

float eval_sh_geometrics(float4 sh, float3 normal)
{
	// http://www.geomerics.com/wp-content/uploads/2015/08/CEDEC_Geomerics_ReconstructingDiffuseLighting1.pdf

    float R0 = sh.x;

    float3 R1 = 0.5f * float3(sh.y, sh.z, sh.w);
    float lenR1 = length(R1);

    float q = 0.5f * (1.0f + dot(R1 / lenR1, normal));

    float p = 1.0f + 2.0f * lenR1 / R0;
    float a = (1.0f - lenR1 / R0) / (1.0f + lenR1 / R0);

    return R0 * (a + (1.0f - a) * (p + 1.0f) * pow(q, p));
}

float3 calSH(float3 sh[4], float3 direction)
{

    float basis[4];
    basis[0] = 0.282095;
    basis[1] = 0.488603 * direction.z;
    basis[2] = 0.488603 * direction.y;
    basis[3] = 0.488603 * direction.x;
    
    return sh[0] * basis[0] + sh[1] * basis[1] + sh[2] * basis[2] + sh[3] * basis[3];

}


[numthreads(8, 4, 1)]
void CS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    GbufferData gbuffer = unpack(GbufferDataPacked::from_uint4(asuint(gbuffer_tex[DispatchThreadID.xy])));
    uint2 ScreenProbeScreenPosition = DispatchThreadID.xy;
    
    
    if (indirectCB.showProbeRadiance == 1)//
    {
        output_tex[DispatchThreadID.xy] = float4(0, 0, 0, 0);
        output_tex[DispatchThreadID.xy] = probe_radiance[DispatchThreadID.xy];
        return;
    }
    
    const float2 uv = get_uv(ScreenProbeScreenPosition, gbuffer_tex_size);
    float depth = depth_tex[ScreenProbeScreenPosition];
    const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, depth);
    
    const float3 normal_vs = geometric_normal_tex[ScreenProbeScreenPosition];
    const float3 normal_ws = direction_view_to_world(normal_vs);
    float3 WorldNormal = gbuffer.normal;
    
    float3 WorldPosition = view_ray_context.ray_hit_ws();
    float SceneDepth = -view_ray_context.ray_hit_vs().z;
    float2 NoiseOffset = 0.0f;

    FScreenProbeSample ScreenProbeSample = (FScreenProbeSample) 0;
    float3 irradiance_sum = 0;

                //UE5里根据true或false来决定使用纹理的可写形式和可读形式，但是这里好像无法做到同时存在纹理的两种形态
                //所以涉及纹理部分全部改成可读写状态
    CalculateUpsampleInterpolationWeights(
					ScreenProbeScreenPosition,
					NoiseOffset,
					WorldPosition,
					SceneDepth, //这里需要传入世界空间下的z坐标
					WorldNormal,
					true,
					ScreenProbeSample);
    
   // uint2 ScreenTileCoord00;
    
    float Epsilon = .01f;
    ScreenProbeSample.Weights /= max(dot(ScreenProbeSample.Weights, 1), Epsilon);
    
    uint4 probeindex;
    for (uint i = 0; i < 4; ++i)
    {
        float3 irradiance = 0;
        uint probeIndex = ScreenProbeSample.AtlasCoord[i].x + ScreenProbeSample.AtlasCoord[i].y * ScreenProbeViewSize.x;
        probeindex[i] = probeIndex;
        for (uint j = 0; j < 3; ++j)
        {
            irradiance[j] += eval_sh_geometrics(probe_sh[probeIndex * 3 + j], WorldNormal);
        }
        irradiance = max(0.0.xxx, irradiance);
        irradiance_sum += irradiance * ScreenProbeSample.Weights[i];

    }
 
    
    float3 my_irradiance_sum = 0;
    for (uint j = 0; j < 4; ++j)
    {
        float3 irradiance = 0;
        uint probeIndex = ScreenProbeSample.AtlasCoord[j].x + ScreenProbeSample.AtlasCoord[j].y * ScreenProbeViewSize.x;
        
        float3 sh[4];
        sh[0] = my_probe_sh[probeIndex * 4];
        sh[1] = my_probe_sh[probeIndex * 4 + 1];
        sh[2] = my_probe_sh[probeIndex * 4 + 2];
        sh[3] = my_probe_sh[probeIndex * 4 + 3];
        
        irradiance = calSH(sh, WorldNormal);
        
        //irradiance = max(0.0.xxx, irradiance);
        my_irradiance_sum += irradiance * ScreenProbeSample.Weights[j];

    }

        output_tex[DispatchThreadID.xy] = float4(irradiance_sum * gbuffer.albedo, 1);
    //显示自适应摆放的探针
    if (false)
    {
        
    
        output_tex[DispatchThreadID.xy] = float4(gbuffer.albedo, 0);

        uint index = DispatchThreadID.x * 32 + DispatchThreadID.y;
        if (index < 4080)
        {
            if (RWAdaptiveScreenProbeData[index] > 0)
            {
                int2 coord = DecodeScreenProbeData(RWAdaptiveScreenProbeData[index]);
                output_tex[coord] = float4(1.0, 0.0, 0.0, 0.0);
                output_tex[coord + uint2(1, 0)] = float4(1.0, 0.0, 0.0, 0.0);
                output_tex[coord + uint2(0, 1)] = float4(1.0, 0.0, 0.0, 0.0);
                output_tex[coord + uint2(1, 1)] = float4(1.0, 0.0, 0.0, 0.0);
            }
        }
    }

    
    
    
   // output_tex[DispatchThreadID.xy] = float4(0.0, 0.0, 0.0, 0.0);
        
    

}
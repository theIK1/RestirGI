#ifndef ADAPTIVE_SCREEN_PROBE_HLSL
#define ADAPTIVE_SCREEN_PROBE_HLSL
#include "uv.hlsl"
#include "LumenCommon.hlsl"

#include "frame_constants.hlsl"

static const float4 gbuffer_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);

struct FScreenProbeSample
{
    uint2 AtlasCoord[4];
    float4 Weights;
};


float GetScreenProbeDepth(uint2 ScreenProbeAtlasCoord)
{
    return RWScreenProbeSceneDepth[ScreenProbeAtlasCoord];
}



float GetScreenProbeDepthFromUAV(uint2 ScreenProbeAtlasCoord)
{
    return RWScreenProbeSceneDepth[ScreenProbeAtlasCoord];
}

float2 GetScreenUVFromScreenTileCoord(uint2 ScreenTileCoord) //把探针序号转成相应UV
{
    uint2 ScreenProbeScreenPosition = ScreenTileCoord * ScreenProbeDownsampleFactor + GetScreenTileJitter(SCREEN_TEMPORAL_INDEX) + (uint2) ViewRectMin.xy;
    return (ScreenProbeScreenPosition + .5f) * BufferSizeAndInvSize.zw;
}

float3 GetWorldPositionFromScreenUV(float2 uv, float depth)
{

    const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, depth);
    return view_ray_context.ray_hit_ws();

}

float2 GetScreenUVFromScreenProbePosition(uint2 ScreenProbeScreenPosition)
{
	// Probe ScreenUV can be outside of valid viewport, since probes are downsampled with DivideAndRoundUp
    float2 ScreenCoord = min((float2) ScreenProbeScreenPosition, ViewRectMin.xy + ViewSizeAndInvSize.xy - 1.0f);
    return (ScreenCoord + .5f) * BufferSizeAndInvSize.zw;
}



uint2 GetScreenTileCoord(uint2 ScreenProbeScreenPosition)
{
    return (ScreenProbeScreenPosition - GetScreenTileJitter(SCREEN_TEMPORAL_INDEX) - (uint2) ViewRectMin.xy) / ScreenProbeDownsampleFactor;
}

uint2 GetAdaptiveProbeCoord(uint2 ScreenTileCoord, uint AdaptiveProbeListIndex)
{
    uint2 AdaptiveProbeCoord = uint2(AdaptiveProbeListIndex % ScreenProbeDownsampleFactor, AdaptiveProbeListIndex / ScreenProbeDownsampleFactor);
	//return ScreenTileCoord * ScreenProbeDownsampleFactor + AdaptiveProbeCoord;
    return AdaptiveProbeCoord * ScreenProbeViewSize + ScreenTileCoord;
}

uint2 GetScreenProbeScreenPosition(uint ScreenProbeIndex)
{
    uint2 ScreenProbeAtlasCoord = uint2(ScreenProbeIndex % ScreenProbeViewSize.x, ScreenProbeIndex / ScreenProbeViewSize.x);
    uint2 ScreenProbeScreenPosition = ScreenProbeAtlasCoord * ScreenProbeDownsampleFactor + GetScreenTileJitter(SCREEN_TEMPORAL_INDEX) + (uint2) ViewRectMin.xy;

    //超出初始摆放探针位置后需要用别的方式计算
    if (ScreenProbeIndex >= NumUniformScreenProbes)
    {
                                                    
        ScreenProbeScreenPosition = DecodeScreenProbeData(RWAdaptiveScreenProbeData[ScreenProbeIndex - NumUniformScreenProbes]);
    }

    return ScreenProbeScreenPosition;

}

float4 testCalculateUniformUpsampleInterpolationWeights
(
	float2 ScreenCoord,
	float2 NoiseOffset,
	float3 WorldPosition,
	float SceneDepth,
	float3 WorldNormal,
	uniform bool bIsUpsamplePass,
	out uint2 ScreenTileCoord00,
	out float4 InterpolationWeights)
{
    uint2 ScreenProbeFullResScreenCoord = clamp(ScreenCoord.xy - ViewRectMin.xy - GetScreenTileJitter(SCREEN_TEMPORAL_INDEX) + NoiseOffset, 0.0f, ViewSizeAndInvSize.xy - 1.0f);
    ScreenTileCoord00 = min(ScreenProbeFullResScreenCoord / ScreenProbeDownsampleFactor, (uint2) ScreenProbeViewSize - 2);

    uint BilinearExpand = 1;
    float2 BilinearWeights = (ScreenProbeFullResScreenCoord - ScreenTileCoord00 * ScreenProbeDownsampleFactor + BilinearExpand) / (float) (ScreenProbeDownsampleFactor + 2 * BilinearExpand);

    //这里是深度缓存中的值，而非世界空间下的深度
    float4 CornerDepths;
    CornerDepths.x = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00) : GetScreenProbeDepthFromUAV(ScreenTileCoord00);
    CornerDepths.y = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00 + int2(1, 0)) : GetScreenProbeDepthFromUAV(ScreenTileCoord00 + int2(1, 0));
    CornerDepths.z = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00 + int2(0, 1)) : GetScreenProbeDepthFromUAV(ScreenTileCoord00 + int2(0, 1));
    CornerDepths.w = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00 + int2(1, 1)) : GetScreenProbeDepthFromUAV(ScreenTileCoord00 + int2(1, 1));

    InterpolationWeights = float4(
		(1 - BilinearWeights.y) * (1 - BilinearWeights.x),
		(1 - BilinearWeights.y) * BilinearWeights.x,
		BilinearWeights.y * (1 - BilinearWeights.x),
		BilinearWeights.y * BilinearWeights.x);

    float4 DepthWeights;

#define PLANE_WEIGHTING 1
#if PLANE_WEIGHTING
	{
        float4 ScenePlane = float4(WorldNormal, dot(WorldPosition, WorldNormal));

        float3 Position00 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00), CornerDepths.x);
        float3 Position10 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00 + uint2(1, 0)), CornerDepths.y);
        float3 Position01 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00 + uint2(0, 1)), CornerDepths.z);
        float3 Position11 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00 + uint2(1, 1)), CornerDepths.w);

        float4 PlaneDistances;
        PlaneDistances.x = abs(dot(float4(Position00, -1), ScenePlane));
        PlaneDistances.y = abs(dot(float4(Position10, -1), ScenePlane));
        PlaneDistances.z = abs(dot(float4(Position01, -1), ScenePlane));
        PlaneDistances.w = abs(dot(float4(Position11, -1), ScenePlane));
			
        float4 RelativeDepthDifference = PlaneDistances / SceneDepth;

        DepthWeights = any(CornerDepths > 0) ? exp2(-10000.0f * (RelativeDepthDifference * RelativeDepthDifference)) : 0;
    }
#else
	{
		float4 DepthDifference = abs(CornerDepths - SceneDepth.xxxx);
		float4 RelativeDepthDifference = DepthDifference / SceneDepth;
		DepthWeights = CornerDepths > 0 ? exp2(-100.0f * (RelativeDepthDifference * RelativeDepthDifference)) : 0;
	}
#endif

    InterpolationWeights *= DepthWeights;
    return DepthWeights;

}

void CalculateUniformUpsampleInterpolationWeights(
	float2 ScreenCoord,
	float2 NoiseOffset,
	float3 WorldPosition,
	float SceneDepth,
	float3 WorldNormal,
	uniform bool bIsUpsamplePass,
	out uint2 ScreenTileCoord00,
	out float4 InterpolationWeights)
{
    uint2 ScreenProbeFullResScreenCoord = clamp(ScreenCoord.xy - ViewRectMin.xy - GetScreenTileJitter(SCREEN_TEMPORAL_INDEX) + NoiseOffset, 0.0f, ViewSizeAndInvSize.xy - 1.0f);
    ScreenTileCoord00 = min(ScreenProbeFullResScreenCoord / ScreenProbeDownsampleFactor, (uint2) ScreenProbeViewSize - 2);

    uint BilinearExpand = 1;
    float2 BilinearWeights = (ScreenProbeFullResScreenCoord - ScreenTileCoord00 * ScreenProbeDownsampleFactor + BilinearExpand) / (float) (ScreenProbeDownsampleFactor + 2 * BilinearExpand);

    //这里是深度缓存中的值，而非世界空间下的深度
    float4 CornerDepths;
    CornerDepths.x = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00) : GetScreenProbeDepthFromUAV(ScreenTileCoord00);
    CornerDepths.y = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00 + int2(1, 0)) : GetScreenProbeDepthFromUAV(ScreenTileCoord00 + int2(1, 0));
    CornerDepths.z = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00 + int2(0, 1)) : GetScreenProbeDepthFromUAV(ScreenTileCoord00 + int2(0, 1));
    CornerDepths.w = bIsUpsamplePass ? GetScreenProbeDepth(ScreenTileCoord00 + int2(1, 1)) : GetScreenProbeDepthFromUAV(ScreenTileCoord00 + int2(1, 1));

    InterpolationWeights = float4(
		(1 - BilinearWeights.y) * (1 - BilinearWeights.x),
		(1 - BilinearWeights.y) * BilinearWeights.x,
		BilinearWeights.y * (1 - BilinearWeights.x),
		BilinearWeights.y * BilinearWeights.x);

    float4 DepthWeights;

#define PLANE_WEIGHTING 1
#if PLANE_WEIGHTING
	{
        float4 ScenePlane = float4(WorldNormal, dot(WorldPosition, WorldNormal));

        float3 Position00 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00), CornerDepths.x);
        float3 Position10 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00 + uint2(1, 0)), CornerDepths.y);
        float3 Position01 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00 + uint2(0, 1)), CornerDepths.z);
        float3 Position11 = GetWorldPositionFromScreenUV(GetScreenUVFromScreenTileCoord(ScreenTileCoord00 + uint2(1, 1)), CornerDepths.w);

        float4 PlaneDistances;
        PlaneDistances.x = abs(dot(float4(Position00, -1), ScenePlane));
        PlaneDistances.y = abs(dot(float4(Position10, -1), ScenePlane));
        PlaneDistances.z = abs(dot(float4(Position01, -1), ScenePlane));
        PlaneDistances.w = abs(dot(float4(Position11, -1), ScenePlane));
			
        float4 RelativeDepthDifference = PlaneDistances / SceneDepth;

        DepthWeights = any(CornerDepths > 0) ? exp2(-10000.0f * (RelativeDepthDifference * RelativeDepthDifference)) : 0;
    }
#else
	{
		float4 DepthDifference = abs(CornerDepths - SceneDepth.xxxx);
		float4 RelativeDepthDifference = DepthDifference / SceneDepth;
		DepthWeights = CornerDepths > 0 ? exp2(-100.0f * (RelativeDepthDifference * RelativeDepthDifference)) : 0;
	}
#endif

    InterpolationWeights *= DepthWeights;
}

void CalculateUpsampleInterpolationWeights(
	float2 ScreenCoord,
	float2 NoiseOffset,
	float3 WorldPosition,
	float SceneDepth,
	float3 WorldNormal,
	uniform bool bIsUpsamplePass,
	out FScreenProbeSample ScreenProbeSample)
{
    uint2 ScreenTileCoord00;
    CalculateUniformUpsampleInterpolationWeights(ScreenCoord, NoiseOffset, WorldPosition, SceneDepth, WorldNormal, bIsUpsamplePass, ScreenTileCoord00, ScreenProbeSample.Weights);

    ScreenProbeSample.AtlasCoord[0] = ScreenTileCoord00;
    ScreenProbeSample.AtlasCoord[1] = ScreenTileCoord00 + uint2(1, 0);
    ScreenProbeSample.AtlasCoord[2] = ScreenTileCoord00 + uint2(0, 1);
    ScreenProbeSample.AtlasCoord[3] = ScreenTileCoord00 + uint2(1, 1);

    bool bUseAdaptiveProbesForUpsample = true;

    if (bUseAdaptiveProbesForUpsample || !bIsUpsamplePass)
    {
        float Epsilon = .01f;
        float4 ScenePlane = float4(WorldNormal, dot(WorldPosition, WorldNormal));

        

        for (uint CornerIndex = 0; CornerIndex < 4; CornerIndex++)
        {
            if (ScreenProbeSample.Weights[CornerIndex] <= Epsilon)
            {
                uint2 ScreenTileCoord = ScreenTileCoord00 + uint2(CornerIndex % 2, CornerIndex / 2);
                uint NumAdaptiveProbes = bIsUpsamplePass ? RWScreenTileAdaptiveProbeHeader[ScreenTileCoord] : RWScreenTileAdaptiveProbeHeader[ScreenTileCoord];

                for (uint AdaptiveProbeListIndex = 0; AdaptiveProbeListIndex < NumAdaptiveProbes; AdaptiveProbeListIndex++)
                {
                    uint2 AdaptiveProbeCoord = GetAdaptiveProbeCoord(ScreenTileCoord, AdaptiveProbeListIndex);
                    uint AdaptiveProbeIndex = bIsUpsamplePass ? RWScreenTileAdaptiveProbeIndices[AdaptiveProbeCoord] : RWScreenTileAdaptiveProbeIndices[AdaptiveProbeCoord];
                    uint ScreenProbeIndex = AdaptiveProbeIndex + NumUniformScreenProbes;

                    uint2 ScreenProbeScreenPosition = bIsUpsamplePass ? GetScreenProbeScreenPosition(ScreenProbeIndex) : DecodeScreenProbeData(RWAdaptiveScreenProbeData[AdaptiveProbeIndex]);
                    uint2 ScreenProbeAtlasCoord = uint2(ScreenProbeIndex % ScreenProbeAtlasViewSize.x, ScreenProbeIndex / ScreenProbeAtlasViewSize.x);
                    float ProbeDepth = bIsUpsamplePass ? GetScreenProbeDepth(ScreenProbeAtlasCoord) : GetScreenProbeDepthFromUAV(ScreenProbeAtlasCoord);
					
                    const float2 uv = get_uv(ScreenProbeScreenPosition, gbuffer_tex_size);
                    const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, ProbeDepth);
                    float ProbeSceneDepth = -view_ray_context.ray_hit_vs().z;
                    
                    float NewDepthWeight = 0;
                    bool bPlaneWeighting = true;
                    if (bPlaneWeighting)
                    {
                        float3 ProbePosition = GetWorldPositionFromScreenUV(GetScreenUVFromScreenProbePosition(ScreenProbeScreenPosition), ProbeDepth);
                        float PlaneDistance = abs(dot(float4(ProbePosition, -1), ScenePlane));
                        float RelativeDepthDifference = PlaneDistance / SceneDepth;
                        NewDepthWeight = exp2(-10000.0f * (RelativeDepthDifference * RelativeDepthDifference));
                    }
                    else
                    {
                        float DepthDifference = abs(ProbeSceneDepth - SceneDepth);
                        float RelativeDepthDifference = DepthDifference / SceneDepth;
                        NewDepthWeight = ProbeSceneDepth > 0 ? exp2(-100.0f * (RelativeDepthDifference * RelativeDepthDifference)) : 0;
                    }

                    float2 DistanceToScreenProbe = abs(ScreenProbeScreenPosition - ScreenCoord);
                    float NewCornerWeight = 1.0f - saturate(min(DistanceToScreenProbe.x, DistanceToScreenProbe.y) / (float) ScreenProbeDownsampleFactor);
                    float NewInterpolationWeight = NewDepthWeight * NewCornerWeight;

                    if (NewInterpolationWeight > ScreenProbeSample.Weights[CornerIndex])
                    {
                        ScreenProbeSample.Weights[CornerIndex] = NewInterpolationWeight;
                        ScreenProbeSample.AtlasCoord[CornerIndex] = ScreenProbeAtlasCoord;
                    }
                }
            }
        }
    }
}

#endif  // ADAPTIVE_SCREEN_PROBE_HLSL

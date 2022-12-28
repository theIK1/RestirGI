#include "gbuffer.hlsl"
#include "uv.hlsl"
#include "LumenCommon.hlsl"
#include "math_const.hlsl"

#include "frame_constants.hlsl"

ConstantBuffer<IndirectCB> indirectCB : register(b1);

Texture2D<float> probe_hit_distance : register(t0);
Texture2D<float4> probe_pre_direction : register(t1);

RWTexture2D<float4> probe_radiance : register(u0);
RWTexture2D<uint> RWScreenTileAdaptiveProbeHeader : register(u1);
RWTexture2D<uint> RWScreenTileAdaptiveProbeIndices : register(u2);
RWStructuredBuffer<uint> RWAdaptiveScreenProbeData : register(u3);
RWStructuredBuffer<uint> RWNumAdaptiveScreenProbes : register(u4);
RWTexture2D<float> RWScreenProbeSceneDepth : register(u5);

#include "adaptiveScreenProbe.hlsl"

uint2 GetUniformScreenProbeScreenPosition(uint2 ScreenTileCoord)
{
    return ScreenTileCoord * ScreenProbeDownsampleFactor + GetScreenTileJitter(SCREEN_TEMPORAL_INDEX) + (uint2) ViewRectMin.xy;
}

float GetFilterPositionWeight(float ProbeDepth, float SceneDepth)
{
    float DepthDifference = abs(ProbeDepth - SceneDepth);
    float RelativeDepthDifference = DepthDifference / SceneDepth;
    return ProbeDepth >= 0 ? exp2(-SpatialFilterPositionWeightScale * (RelativeDepthDifference * RelativeDepthDifference)) : 0;
}

//
// Trigonometric functions
//

// max absolute error 9.0x10^-3
// Eberly's polynomial degree 1 - respect bounds
// 4 VGPR, 12 FR (8 FR, 1 QR), 1 scalar
// input [-1, 1] and output [0, PI]
float acosFast(float inX)
{
    float x = abs(inX);
    float res = -0.156583f * x + (0.5 * M_PI);
    res *= sqrt(1.0f - x);
    return (inX >= 0) ? res : M_PI - res;
}

void GatherNeighborRadiance(
	int2 NeighborScreenTileCoord,
	uint2 ProbeTexelCoord,
	float3 WorldPosition,
	float3 WorldConeDirection,
	float SceneDepth,
    float HitDistance,
	inout float3 TotalRadiance,
	inout float TotalWeight)
{
    if (all(NeighborScreenTileCoord >= 0) && all(NeighborScreenTileCoord < (int2) ScreenProbeViewSize))
    {
        uint2 NeighborScreenProbeAtlasCoord = NeighborScreenTileCoord;
        uint2 NeighborScreenProbeScreenPosition = GetUniformScreenProbeScreenPosition(NeighborScreenProbeAtlasCoord);
        
        float NeighborDepth = GetScreenProbeDepthFromUAV(NeighborScreenProbeAtlasCoord);
        const float2 uv = get_uv(NeighborScreenProbeScreenPosition, gbuffer_tex_size);
        const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, NeighborDepth);
 
        float NeighborSceneDepth = -view_ray_context.ray_hit_vs().z; 

        float PositionWeight = GetFilterPositionWeight(NeighborSceneDepth, SceneDepth);

		
        if (PositionWeight > 0.0f)
        {
            uint2 NeighborTraceCoord = NeighborScreenProbeAtlasCoord * ScreenProbeGatherOctahedronResolution + ProbeTexelCoord;
            float NeighborRadianceDepth = probe_hit_distance.Load(int3(NeighborTraceCoord, 0));

            if (NeighborRadianceDepth >= 0)
            {
				// Clamp neighbor's hit distance to our own.  This helps preserve contact shadows, as a long neighbor hit distance will cause a small NeighborAngle and bias toward distant lighting.
                if (HitDistance >= 0)
                {
                    NeighborRadianceDepth = min(NeighborRadianceDepth, HitDistance);
                }
                
                float3 NeighborWorldPosition = view_ray_context.ray_hit_ws();
                float3 NeighborHitPosition = NeighborWorldPosition + WorldConeDirection * NeighborRadianceDepth;
                float3 ToNeighborHit = NeighborHitPosition - WorldPosition;
                float NeighborAngle = acosFast(dot(ToNeighborHit, WorldConeDirection) / length(ToNeighborHit));
                float AngleWeight = 1.0f - saturate(NeighborAngle / SpatialFilterMaxRadianceHitAngle);

                float Weight = PositionWeight * AngleWeight;
       
                TotalRadiance += probe_radiance.Load(int3(NeighborTraceCoord, 0)).xyz * Weight;
                TotalWeight += Weight;
            }
        }
    }
}

[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    if (indirectCB.enableFilter == 0)
        return;
    
    uint ScreenProbeIndex = GroupId.x + ScreenProbeViewSize.x * GroupId.y;
    if (ScreenProbeIndex >= NumUniformScreenProbes)// + RWNumAdaptiveScreenProbes[0]
    {
        return;
    }
    
    

    uint2 ScreenProbeScreenPosition = GetScreenProbeScreenPosition(ScreenProbeIndex);
    uint2 ScreenTileCoord = GetScreenTileCoord(ScreenProbeScreenPosition);
    //uint2 ProbeTexelCoord = DispatchThreadId.xy - ScreenTileCoord * ScreenProbeGatherOctahedronResolution;
    uint2 ProbeTexelCoord = GroupThreadId.xy;

    const float2 uv = get_uv(ScreenProbeScreenPosition, gbuffer_tex_size);
    float depth = RWScreenProbeSceneDepth[ScreenTileCoord];
    const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, depth);
 
    float SceneDepth = -view_ray_context.ray_hit_vs().z; //这里应是 GetScreenProbeDepth

        if (SceneDepth > 0.0f)
        {
            
            float3 WorldPosition = view_ray_context.ray_hit_ws();

            float2 ProbeTexelCenter = float2(0.5, 0.5);
            float2 ProbeUV = (ProbeTexelCoord + ProbeTexelCenter) / (float) ScreenProbeGatherOctahedronResolution;
        float3 WorldConeDirection = probe_pre_direction.Load(int3(DispatchThreadId.xy, 0)).xyz;
        float hitDistance = probe_hit_distance.Load(int3(DispatchThreadId.xy, 0));
            float3 TotalRadiance = 0;
            float TotalWeight = 0;

            TotalRadiance = probe_radiance.Load(int3(DispatchThreadId.xy, 0)).xyz;

            TotalWeight = 1.0f;
            

            int2 Offsets[4];
            Offsets[0] = int2(-1, 0);
            Offsets[1] = int2(1, 0);
            Offsets[2] = int2(0, -1);
            Offsets[3] = int2(0, 1);

//            LOOP

            for (uint OffsetIndex = 0; OffsetIndex < 4; OffsetIndex++)
            {
            GatherNeighborRadiance(ScreenTileCoord + Offsets[OffsetIndex], ProbeTexelCoord, WorldPosition, WorldConeDirection, SceneDepth, hitDistance, TotalRadiance, TotalWeight);
        }

            if (TotalWeight > 0)
            {
                TotalRadiance /= TotalWeight;
            }

            probe_radiance[DispatchThreadId.xy] = float4(TotalRadiance, probe_radiance[DispatchThreadId.xy].w);

    }
    else
        probe_radiance[DispatchThreadId.xy] = float4(0, 0, 0, probe_radiance[DispatchThreadId.xy].w);
        
    

}
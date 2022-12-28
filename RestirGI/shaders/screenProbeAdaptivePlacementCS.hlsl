#include "gbuffer.hlsl"
#include "uv.hlsl"
#include "LumenCommon.hlsl"

#include "frame_constants.hlsl"

Texture2D<float3> geometric_normal_tex : register(t0);
Texture2D<float> depth_tex : register(t1);
Texture2D<float4> gbuffer_tex : register(t2);



RWTexture2D<uint> RWScreenTileAdaptiveProbeHeader : register(u0);
RWTexture2D<uint> RWScreenTileAdaptiveProbeIndices : register(u1);
RWStructuredBuffer<uint> RWAdaptiveScreenProbeData : register(u2);
RWStructuredBuffer<uint> RWNumAdaptiveScreenProbes : register(u3);
RWTexture2D<float> RWScreenProbeSceneDepth : register(u4);
RWTexture2D<float4> RWtest : register(u5);

ConstantBuffer<ScreenProbeAdaptivePlacementCB> screenProbeAdaptivePlacementCB : register(b1);

#include "downsample.hlsl"
#include "adaptiveScreenProbe.hlsl"

groupshared uint SharedNumProbesToAllocate;
groupshared uint SharedAdaptiveProbeBaseIndex;
groupshared uint2 SharedProbeScreenPositionsToAllocate[8 * 8];
groupshared FScreenProbeGBuffer SharedScreenProbeGBuffer[8 * 8];





[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    uint ThreadIndex = GroupThreadId.y * 8 + GroupThreadId.x;

    if (ThreadIndex == 0)
    {
        SharedNumProbesToAllocate = 0;
    }

    GroupMemoryBarrierWithGroupSync();
    
    	{
        uint2 ScreenProbeScreenPosition = DispatchThreadId.xy * screenProbeAdaptivePlacementCB.PlacementDownsampleFactor + GetScreenTileJitter(SCREEN_TEMPORAL_INDEX) + (uint2) ViewRectMin.xy;

        
        
        if (all(ScreenProbeScreenPosition < (uint2) (ViewRectMin.xy + ViewSizeAndInvSize.xy)) && any((DispatchThreadId.xy & 0x1) != 0))
        {
            const float2 uv = get_uv(ScreenProbeScreenPosition, gbuffer_tex_size);
            float depth = depth_tex[ScreenProbeScreenPosition];
            
     
            
            const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, depth);
        
            const float3 normal_vs = geometric_normal_tex[ScreenProbeScreenPosition];
            const float3 normal_ws = direction_view_to_world(normal_vs);
        
            GbufferData gbuffer = unpack(GbufferDataPacked::from_uint4(asuint(gbuffer_tex[ScreenProbeScreenPosition])));
            
            FGBufferData GBufferData;
            GBufferData.WorldNormal = gbuffer.normal;
            GBufferData.WorldPosition = float4(view_ray_context.ray_hit_ws(), 0);
            GBufferData.Depth = depth_tex[ScreenProbeScreenPosition];
            
            float SceneDepth = -view_ray_context.ray_hit_vs().z;
      

            if (GBufferData.Depth > 0.0)
            {
                
                float3 WorldPosition = GBufferData.WorldPosition.xyz;
                float2 NoiseOffset = 0.0f;

                FScreenProbeSample ScreenProbeSample = (FScreenProbeSample) 0;

                //UE5里根据true或false来决定使用纹理的可写形式和可读形式，但是这里好像无法做到同时存在纹理的两种形态
                //所以涉及纹理部分全部改成可读写状态
                CalculateUpsampleInterpolationWeights(
					ScreenProbeScreenPosition,
					NoiseOffset,
					WorldPosition,
					SceneDepth, //这里需要传入世界空间下的z坐标
					GBufferData.WorldNormal,
					false,
					ScreenProbeSample);

                
                
                float Epsilon = .01f;
                ScreenProbeSample.Weights /= max(dot(ScreenProbeSample.Weights, 1), Epsilon);

                float LightingIsValid = (dot(ScreenProbeSample.Weights, 1) < 1.0f - Epsilon) ? 0.0f : 1.0f;

                if (!LightingIsValid) // 
                {
                    uint SharedListIndex;
                    InterlockedAdd(SharedNumProbesToAllocate, 1, SharedListIndex);
                    SharedProbeScreenPositionsToAllocate[SharedListIndex] = ScreenProbeScreenPosition;
                    SharedScreenProbeGBuffer[SharedListIndex] = GetScreenProbeGBuffer(GBufferData);
                }
            }
        }
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (ThreadIndex == 0 && RWNumAdaptiveScreenProbes[0] < 4080)
    {
        InterlockedAdd(RWNumAdaptiveScreenProbes[0], SharedNumProbesToAllocate, SharedAdaptiveProbeBaseIndex);
    }

    GroupMemoryBarrierWithGroupSync();

    uint AdaptiveProbeIndex = ThreadIndex + SharedAdaptiveProbeBaseIndex;

    if (ThreadIndex < SharedNumProbesToAllocate && AdaptiveProbeIndex < MaxNumAdaptiveProbes)
    {
        uint2 ScreenProbeScreenPosition = SharedProbeScreenPositionsToAllocate[ThreadIndex];
        RWAdaptiveScreenProbeData[AdaptiveProbeIndex] = EncodeScreenProbeData(ScreenProbeScreenPosition);
        uint2 ScreenTileCoord = GetScreenTileCoord(ScreenProbeScreenPosition);

        uint TileProbeIndex;
        InterlockedAdd(RWScreenTileAdaptiveProbeHeader[ScreenTileCoord], 1, TileProbeIndex);
        uint2 AdaptiveProbeCoord = GetAdaptiveProbeCoord(ScreenTileCoord, TileProbeIndex);
        RWScreenTileAdaptiveProbeIndices[AdaptiveProbeCoord] = AdaptiveProbeIndex;
		
        float2 ScreenUV = (ScreenProbeScreenPosition + .5f) * BufferSizeAndInvSize.zw;
        uint ScreenProbeIndex = NumUniformScreenProbes + AdaptiveProbeIndex;
        uint2 ScreenProbeAtlasCoord = uint2(ScreenProbeIndex % ScreenProbeAtlasViewSize.x, ScreenProbeIndex / ScreenProbeAtlasViewSize.x);
        WriteDownsampledProbeGBuffer(ScreenUV, ScreenProbeAtlasCoord, SharedScreenProbeGBuffer[ThreadIndex]);
    }

}
#include "uv.hlsl"
#include "LumenCommon.hlsl"

#include "frame_constants.hlsl"

Texture2D<float3> geometric_normal_tex : register(t0);
Texture2D<float> depth_tex : register(t1);


RWTexture2D<float> RWScreenProbeSceneDepth : register(u0);
RWTexture2D<float4> RWtest : register(u1);


static const float4 gbuffer_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);

#include "downsample.hlsl"



[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{

    
    uint2 ScreenProbeAtlasCoord = DispatchThreadId.xy;
    
    if (all(ScreenProbeAtlasCoord < ScreenProbeViewSize))
    {
        uint2 ScreenJitter = GetScreenTileJitter(SCREEN_TEMPORAL_INDEX);
        
        uint2 ScreenProbeScreenPosition = min((uint2) (ScreenProbeAtlasCoord * ScreenProbeDownsampleFactor + ScreenJitter), (uint2) (ViewRectMin.xy + ViewSizeAndInvSize.xy) - 1);
        float2 ScreenUV = float2(0.0, 0.0);
    
        const float2 uv = get_uv(ScreenProbeScreenPosition, gbuffer_tex_size);
        float depth = depth_tex[ScreenProbeScreenPosition];
        const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, depth);
        
        const float3 normal_vs = geometric_normal_tex[ScreenProbeScreenPosition];
        const float3 normal_ws = direction_view_to_world(normal_vs);
        
        FGBufferData GBufferData;
        GBufferData.WorldNormal = normal_ws;
        GBufferData.WorldPosition = float4(view_ray_context.ray_hit_ws(),0);
        GBufferData.Depth = depth;
        
        
        
        WriteDownsampledProbeGBuffer(ScreenUV, ScreenProbeAtlasCoord, GetScreenProbeGBuffer(GBufferData));
    }
    else
    {
        float2 ScreenUV = float2(0.0, 0.0);
        FGBufferData GBufferData;
        GBufferData.WorldNormal = 0;
        GBufferData.WorldPosition = 0;
        GBufferData.Depth = 0;
        
        
        
        WriteDownsampledProbeGBuffer(ScreenUV, ScreenProbeAtlasCoord, GetScreenProbeGBuffer(GBufferData));
    }
        
    

}
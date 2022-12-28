#ifndef DOWNSAMPLE_HLSL
#define DOWNSAMPLE_HLSL

struct FScreenProbeGBuffer
{
    float3 WorldNormal;
    float SceneDepth;
    bool bLit;
};

struct FGBufferData
{
    float3 WorldNormal;
    float Depth;
    float4 WorldPosition;
};



FScreenProbeGBuffer GetScreenProbeGBuffer(FGBufferData GBufferData)
{
    FScreenProbeGBuffer ScreenProbeGBuffer;
    ScreenProbeGBuffer.WorldNormal = GBufferData.WorldNormal;
    ScreenProbeGBuffer.SceneDepth = GBufferData.Depth;
    ScreenProbeGBuffer.bLit = true;
    //ScreenProbeGBuffer.bLit = GBufferData.ShadingModelID != SHADINGMODELID_UNLIT;
    return ScreenProbeGBuffer;
}

void WriteDownsampledProbeGBuffer(float2 ScreenUV, uint2 ScreenProbeAtlasCoord, FScreenProbeGBuffer ProbeGBuffer)
{
    
    RWScreenProbeSceneDepth[ScreenProbeAtlasCoord] = ProbeGBuffer.SceneDepth;

}

#endif
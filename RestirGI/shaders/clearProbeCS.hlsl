



RWTexture2D<uint> RWScreenTileAdaptiveProbeHeader : register(u0); 
RWTexture2D<uint> RWScreenTileAdaptiveProbeIndices : register(u1); 
RWStructuredBuffer<uint> RWAdaptiveScreenProbeData : register(u2);
RWStructuredBuffer<uint> RWNumAdaptiveScreenProbes : register(u3); 

RWStructuredBuffer<float> RWRMSEBuffer : register(u4);



[numthreads(8, 4, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    uint dispatch_idx = DispatchThreadId.x;
    if (dispatch_idx == 0)
    {
        RWRMSEBuffer[0] = 0;
        RWRMSEBuffer[1] = 0;
        RWRMSEBuffer[2] = 0;
        RWNumAdaptiveScreenProbes[0] = 0;
    }
        
    RWScreenTileAdaptiveProbeIndices[DispatchThreadId.xy] = 0;
    if (DispatchThreadId.x < 120 && DispatchThreadId.y < 68)
        RWScreenTileAdaptiveProbeHeader[DispatchThreadId.xy] = 0;
    
    uint index = DispatchThreadId.x * 32 + DispatchThreadId.y;
    if (index < 4080)
        RWAdaptiveScreenProbeData[index] = 0;
        
    

}
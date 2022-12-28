

Texture2D<float> input : register(t0);


RWTexture2D<float> RWoutput : register(u0);





[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    
    RWoutput[DispatchThreadId.xy] = 1.0 - input[DispatchThreadId.xy];


}
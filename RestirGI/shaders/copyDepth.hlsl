


Texture2D<float> Depth : register(t0);

RWTexture2D<float> PrevDepth : register(u0);





[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{

    PrevDepth[DispatchThreadId.xy] = Depth[DispatchThreadId.xy];
   

}
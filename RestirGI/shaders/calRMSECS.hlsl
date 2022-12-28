

Texture2D<float4> source_tex : register(t0);
Texture2D<float4> target_tex : register(t1);

RWTexture2D<float4> output_tex : register(u0);





[numthreads(8, 4, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    
    output_tex[DispatchThreadId.xy] = abs(source_tex[DispatchThreadId.xy] - target_tex[DispatchThreadId.xy]);
    output_tex[DispatchThreadId.xy].x = 0;
    output_tex[DispatchThreadId.xy].y = 0;

}
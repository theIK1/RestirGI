

Texture2D<float4> source_tex : register(t0);


RWStructuredBuffer<float> RWbuf : register(u0);


static const uint N = 1920 * 1080;


[numthreads(8, 4, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    float X2 = source_tex[DispatchThreadId.xy].x * source_tex[DispatchThreadId.xy].x;
    float Y2 = source_tex[DispatchThreadId.xy].y * source_tex[DispatchThreadId.xy].y;
    float Z2 = source_tex[DispatchThreadId.xy].z * source_tex[DispatchThreadId.xy].z;
    RWbuf[0] += X2 / N;//
    RWbuf[1] += Y2 / N;
    RWbuf[2] += Z2 / N;
    

}
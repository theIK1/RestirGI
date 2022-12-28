

RWTexture2D<float4> output_tex : register(u0);


[numthreads(8, 8, 1)]
void CS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    output_tex[DispatchThreadId.xy] = float4(0,0,0,0);
}

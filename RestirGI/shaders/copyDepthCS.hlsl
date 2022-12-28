


Texture2D<float> input_tex : register(t0);

RWTexture2D<float> output_tex : register(u0);





[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{

    output_tex[DispatchThreadId.xy] = input_tex[DispatchThreadId.xy];
   

}

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

//every post processing use this common vertex shader
PSInput VSMain(float4 position : POSITION, float2 texCoord : TEXCOORD)
{
    PSInput result;
    result.position = position;
    result.texCoord = texCoord;  
    return result;
}

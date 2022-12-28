
Texture2D gDiffuseMap : register(t0); //ËùÓÐÂþ·´ÉäÌùÍ¼
SamplerState gsamLinear : register(s3);


struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

cbuffer ObjectConstant : register(b1)
{
    float4x4 world;
}

cbuffer CameraParams : register(b0)
{
    float4x4 view;
    float4x4 projection;
}

//PSInput VSMain(float4 position : POSITION, float4 color : COLOR) {
PSInput VSMain(float4 position : POSITION, float3 normal : NORMAL, float2 texCoord : TEXCOORD, uint meshID : MESHID)
{
    PSInput result;

    float4 pos = position;
    pos = mul(world, pos);
    pos = mul(view, pos);
    pos = mul(projection, pos);
    result.position = pos;
  //result.color = color; 
    result.normal = normal;
    result.texCoord = texCoord;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{	
    return gDiffuseMap.SampleLevel(gsamLinear, input.texCoord, 0.0);
}

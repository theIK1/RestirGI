

struct PSLightInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

Texture2D gAlbedoTexture : register(t1);
Texture2D gNormalTexture : register(t2);
Texture2D gSpecularGlossTexture : register(t3);
Texture2D gDepth : register(t4);

SamplerState gsamLinear : register(s0);

PSLightInput VSMain(float4 position : POSITION,float2 texCoord : TEXCOORD)
{
    PSLightInput output;
    output.position = position;
    output.texCoord = texCoord;
    return output;
}

float4 PSMain(PSLightInput input) : SV_TARGET
{	
    float z = gDepth[input.position.xy];
    float3 normal = normalize(gNormalTexture[input.position.xy].xyz);
    float4 albedo = gAlbedoTexture[input.position.xy];
    
    return albedo;
}

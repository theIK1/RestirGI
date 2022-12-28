Texture2D<float4> raytracingTex : register(t0);

cbuffer TemporalParams : register(b0)
{
    int bTemporal;

}

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

// Luminance
float CalculateLuminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 PSMain(PSInput input) : SV_Target
{
    float4 color;
    
    color = raytracingTex[input.position.xy];
    float variance = CalculateLuminance(color.rgb);
    color.a = variance;
    
    return color;
}

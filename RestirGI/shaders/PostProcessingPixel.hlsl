#include "gbuffer.hlsl"

Texture2D<float4> colorTex : register(t0);
Texture2D<float4> gbufferTex : register(t1);
Texture2D<float3> normalTex : register(t2);
Texture2D<float4> rasterPositionTex : register(t3);
Texture2D         rasterDepthTex : register(t4);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

//enum ShowRenderType
//{
//    PATH_TRACTING = 0,
//		DIRECT_LIGHT = 1,
//		ALBEDO = 2,
//		DEPTH = 3,
//		NORMAL = 4,
//		AO = 5,
//		POSITION = 6,
//		RESTIR = 7
//	};


cbuffer ShowRenderType : register(b0)
{
    int showRenderType;
}

float4 PSMain(PSInput input) : SV_Target
{
    float2 pos = input.position.xy;
    float4 color = colorTex[pos];
    GbufferData gbuffer = unpack(GbufferDataPacked::from_uint4(asuint(gbufferTex[pos])));
    
    
    int Type = showRenderType;

    switch (Type)
    {

        case 2: //ALBEDO
            color.rgb = gbuffer.albedo;
            break;
        case 3:
            float depth = rasterDepthTex[pos];
            if (depth < 0.99f)
                depth = 0.9901f;
            depth = (depth - 0.99) * 100.0f;
            color = float4(depth, depth, depth, 1.f);
            break;
        case 4:
            color.rgb = normalTex[pos];
            break;

        case 6:
            color = rasterPositionTex[pos] / 100.f;
            break;
        default:
            break;

    }
    
    return color;
}

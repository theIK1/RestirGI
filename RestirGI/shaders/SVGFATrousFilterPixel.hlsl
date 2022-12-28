#include "../structure.h"
Texture2D colorMap : register(t0);

Texture2D rasterAlbedoMap : register(t1);
Texture2D rasterNormalMap : register(t2);
Texture2D rasterPositionMap : register(t3);


ConstantBuffer<AtrousConstantBuffer> AtrousParams : register(b0);


struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

// Calculate Normal Weight
float CalculateNormalWeight(float3 normalP, float3 normalQ, float phiNormal)
{
	// Experience phiNormal : 128 
    float normalWeight = pow(max(0.0, dot(normalP, normalQ)), phiNormal);
    return normalWeight;
}

// Calculate Depth Weight
float CalculateDepthWeight(float depthP, float depthQ, float phiDepth, float offsetLength, float2 posP)
{
	// Experience phiDepth : 1
    float epsilon = 1e-10;

    float deltaDepthP = rasterPositionMap.Load(int3(posP, 0)).a;

	// experiment
	//float2 posX = posP + float2(1, 0);
	//float2 posY = posP + float2(0, 1);
	//float ddx = depthMap.Load(int3(posX, 0)).a - depthMap.Load(int3(posP, 0)).a;
	//float ddy = depthMap.Load(int3(posY, 0)).a - depthMap.Load(int3(posP, 0)).a;
	//float deltaDepthP = max(abs(ddx), abs(ddy));

    float depthWeight = exp((-abs(depthP - depthQ)) / (phiDepth * abs(deltaDepthP * offsetLength) + epsilon));
    return depthWeight;
}

// Luminance
float CalculateLuminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// Calculate Luminance Weight
float CalculateLuminanceWeight(float luminanceP, float luminanceQ, float phiLuminance, float gaussionLumVarP)
{
    float epsilon = 1e-10;
    float luminanceWeight = exp(-abs(luminanceP - luminanceQ) / (phiLuminance * sqrt(gaussionLumVarP) + epsilon));
    return luminanceWeight;
}

//--------------------------------------------------------------
//	PSMain
//--------------------------------------------------------------
float4 PSMain(VS_OUTPUT input) : SV_TARGET
{
    
    
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    
    if (AtrousParams.atrousType == 0)
    {
        float2 uv = input.pos.xy;

        float4 colorValue = colorMap.Load(int3(uv, 0));
        float4 normalValue = rasterNormalMap.Load(int3(uv, 0));
        float4 posValue = rasterPositionMap.Load(int3(uv, 0));
        float4 depthValue = rasterAlbedoMap.Load(int3(uv, 0));

        const float kernel[25] =
        {
            1.0f / 256.0f, 1.0f / 64.0f, 3.0f / 128.0f, 1.0f / 64.0f, 1.0f / 256.0f,
		1.0f / 64.0f, 1.0f / 16.0f, 3.0f / 32.0f, 1.0f / 16.0f, 1.0f / 64.0f,
		3.0f / 128.0f, 3.0f / 32.0f, 9.0f / 64.0f, 3.0f / 32.0f, 3.0f / 128.0f,
		1.0f / 64.0f, 1.0f / 16.0f, 3.0f / 32.0f, 1.0f / 16.0f, 1.0f / 64.0f,
		1.0f / 256.0f, 1.0f / 64.0f, 3.0f / 128.0f, 1.0f / 64.0f, 1.0f / 256.0f
        };

        const float2 offset[25] =
        {
            { -2, -2 },
            { -1, -2 },
            { 0, -2 },
            { 1, -2 },
            { 2, -2 },
            { -2, -1 },
            { -1, -1 },
            { 0, -1 },
            { 1, -1 },
            { 2, -1 },
            { -2, 0 },
            { -1, 0 },
            { 0, 0 },
            { 1, 0 },
            { 2, 0 },
            { -2, 1 },
            { -1, 1 },
            { 0, 1 },
            { 1, 1 },
            { 2, 1 },
            { -2, 2 },
            { -1, 2 },
            { 0, 2 },
            { 1, 2 },
            { 2, 2 }
        };

        float4 sum = float4(0.0, 0.0, 0.0, 0.0);
        float cum_w = 0.0;

        for (int i = 0; i < 25; i++)
        {
            const int2 p = uv + offset[i] * AtrousParams.stepWidth;


            float4 colorTemp = colorMap.Load(int3(p, 0));
            float3 t = colorValue - colorTemp;
            float dist2 = dot(t, t);
            float colorWeight = min(exp(-(dist2) / AtrousParams.colorPhi), 1.0);

            float4 normalTemp = rasterNormalMap.Load(int3(p, 0));
            t = normalValue - normalTemp;
            dist2 = max(dot(t, t) / (AtrousParams.stepWidth * AtrousParams.stepWidth), 0.0);
            float normalWeight = min(exp(-(dist2) / AtrousParams.normalPhi), 1.0);

            float4 posTemp = rasterPositionMap.Load(int3(p, 0));
            t = posValue - posTemp;
            dist2 = dot(t, t);
            float posWeight = min(exp(-(dist2) / AtrousParams.posPhi), 1.0);

            float totalWeight = colorWeight * normalWeight * posWeight;
            sum += colorTemp * totalWeight;
            cum_w += totalWeight;
        }
    
        color = sum / cum_w;

    }
    else
    {
        float2 posP = input.pos.xy;

        float4 colorP = colorMap.Load(int3(posP, 0)); 
        float luminanceP = CalculateLuminance(colorP.rgb);
        float variance = colorP.a;

        float3 normalP = rasterNormalMap.Load(int3(posP, 0)).rgb; 
        float depthP = rasterAlbedoMap.Load(int3(posP, 0)).a; 

        const float kernelWeights[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };


        float4 sum = colorP;
        float sumWeight = 1.0;

        for (int yy = -2; yy <= 2; yy++)
        {
            for (int xx = -2; xx <= 2; xx++)
            {
                if (xx == 0 && yy == 0)
                {
                    continue;
                }

                const int2 posQ = posP + int2(xx, yy) * AtrousParams.stepWidth;
                const float kernel = kernelWeights[abs(xx)] * kernelWeights[abs(yy)];

                float4 colorQ = colorMap.Load(int3(posQ, 0));
            
                float luminanceQ = CalculateLuminance(colorQ.rgb);
                float luminanceWeight = CalculateLuminanceWeight(luminanceP, luminanceQ, AtrousParams.phiLuminance, variance);

                float depthQ = rasterAlbedoMap.Load(int3(posQ, 0)).a;
                float depthWeight = CalculateDepthWeight(depthP, depthQ, AtrousParams.phiDepth, AtrousParams.stepWidth * length(float2(xx, yy)), posP);

                float3 normalQ = rasterNormalMap.Load(int3(posQ, 0)).rgb;
                normalP = normalize(normalP);
                normalQ = normalize(normalQ);
                float normalWeight = CalculateNormalWeight(normalP, normalQ, AtrousParams.phiNormal);

                float totalWeight = luminanceWeight * depthWeight * normalWeight;

                sum.rgb += colorQ.rgb * totalWeight * kernel;
                sum.a += colorQ.a * totalWeight * totalWeight * kernel * kernel;
                sumWeight += totalWeight * kernel;
            }
        }

        color.rgb = sum.rgb / sumWeight;
        color.a = sum.a / (sumWeight * sumWeight);
    }
    
    return color;
}

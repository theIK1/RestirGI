#ifndef LUMEN_COMMAN_HLSL
#define LUMEN_COMMAN_HLSL
#include"LumenMath.hlsl"

float2 Hammersley16(uint Index, uint NumSamples, uint2 Random)
{
    float E1 = frac((float) Index / NumSamples + float(Random.x) * (1.0 / 65536.0));
    float E2 = float((reversebits(Index) >> 16) ^ Random.y) * (1.0 / 65536.0);
    return float2(E1, E2);
}

uint2 GetScreenTileJitter(uint TemporalIndex)
{
    
    return Hammersley16(TemporalIndex, 8, 0) * ScreenProbeDownsampleFactor;
}

uint EncodeScreenProbeData(uint2 ScreenProbeScreenPosition)
{
    return (ScreenProbeScreenPosition.x & 0xFFFF) | ((ScreenProbeScreenPosition.y & 0xFFFF) << 16);
}

uint2 DecodeScreenProbeData(uint EncodedProbeData)
{
    return uint2(EncodedProbeData & 0xFFFF, (EncodedProbeData >> 16) & 0xFFFF);
}

#endif
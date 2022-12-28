#include "atmosphere.hlsl"
#include "sun.hlsl"
#include "cube_map.hlsl"

RWTexture2DArray<float4> output_tex : register(u0);

[numthreads(8, 8, 1)]
void CS(in uint3 px : SV_DispatchThreadID) {
    uint face = px.z;
    float2 uv = (px.xy + 0.5) / 64;
    float3 dir = normalize(mul(CUBE_MAP_FACE_ROTATIONS[face], float3(uv * 2 - 1, -1.0)));

    //float3 output = dir * 0.5 + 0.5;
    float3 output = atmosphere_default(dir, SUN_DIRECTION);

    output_tex[px] = float4(output, 1);
}

#ifndef RT_SHADERINTEROP_H
#define RT_SHADERINTEROP_H

#ifdef __cplusplus // not invoking shader compiler, but included in engine source
#include <DirectXMath.h>
using namespace DirectX;

typedef XMMATRIX matrix;
typedef XMFLOAT3X3 float3x3;
typedef XMFLOAT3X4 float3x4;
typedef XMFLOAT4X4 float4x4;
typedef XMFLOAT2 float2;
typedef XMFLOAT3 float3;
typedef XMFLOAT4 float4;
typedef uint32_t uint;
typedef XMUINT2 uint2;
typedef XMUINT3 uint3;
typedef XMUINT4 uint4;
typedef XMINT2 int2;
typedef XMINT3 int3;
typedef XMINT4 int4;


#endif // __cplusplus

#endif //RT_SHADERINTEROP_H

#ifndef DESCRIPTORS_HLSL
#define DESCRIPTORS_HLSL
//#include "xxx.h"


// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.







#define MAX_AO_RAYS (4)


struct HitInfo
{
    float4 colorAndDistance;
    
};

struct PackedPayload
{ // Byte Offset        Data Format
    float hitT; // 0                  HitT
    float3 worldPosition; // 4               X: World Position X
                                       // 8               Y: World Position Y
                                       // 12              Z: World Position Z
    uint4 packed0; // 16              X: 16: Albedo R          16: Albedo G
                                       //                 Y: 16: Albedo B          16: Normal X
                                       //                 Z: 16: Normal Y          16: Normal Z
                                       //                 W: 16: Metallic          16: Roughness
    uint3 packed1; // 32              X: 16: ShadingNormal X   16: ShadingNormal Y
                                       //                 Y: 16: ShadingNormal Z   16: Opacity
                                       //                 Z: 16: Hit Kind          16: Unused
                                       // 44
};

struct Payload
{ // Byte Offset
    float3 albedo; // 12
    float opacity; // 16
    float3 worldPosition; // 28
    float metallic; // 32
    float3 normal; // 44
    float roughness; // 48
    float3 shadingNormal; // 60
    float hitT; // 64
    uint hitKind; // 68
};

Payload UnpackPayload(PackedPayload input)
{
    Payload output = (Payload) 0;
    output.hitT = input.hitT;
    output.worldPosition = input.worldPosition;

    output.albedo.r = f16tof32(input.packed0.x);
    output.albedo.g = f16tof32(input.packed0.x >> 16);
    output.albedo.b = f16tof32(input.packed0.y);
    output.normal.x = f16tof32(input.packed0.y >> 16);
    output.normal.y = f16tof32(input.packed0.z);
    output.normal.z = f16tof32(input.packed0.z >> 16);
    output.metallic = f16tof32(input.packed0.w);
    output.roughness = f16tof32(input.packed0.w >> 16);

    output.shadingNormal.x = f16tof32(input.packed1.x);
    output.shadingNormal.y = f16tof32(input.packed1.x >> 16);
    output.shadingNormal.z = f16tof32(input.packed1.y);
    output.opacity = f16tof32(input.packed1.y >> 16);
    output.hitKind = f16tof32(input.packed1.z);

    return output;
}

PackedPayload PackPayload(Payload input)
{
    PackedPayload output = (PackedPayload) 0;
    output.hitT = input.hitT;
    output.worldPosition = input.worldPosition;

    output.packed0.x = f32tof16(input.albedo.r);
    output.packed0.x |= f32tof16(input.albedo.g) << 16;
    output.packed0.y = f32tof16(input.albedo.b);
    output.packed0.y |= f32tof16(input.normal.x) << 16;
    output.packed0.z = f32tof16(input.normal.y);
    output.packed0.z |= f32tof16(input.normal.z) << 16;
    output.packed0.w = f32tof16(input.metallic);
    output.packed0.w |= f32tof16(input.roughness) << 16;

    output.packed1.x = f32tof16(input.shadingNormal.x);
    output.packed1.x |= f32tof16(input.shadingNormal.y) << 16;
    output.packed1.y = f32tof16(input.shadingNormal.z);
    output.packed1.y |= f32tof16(input.opacity) << 16;
    output.packed1.z = f32tof16(input.hitKind);
  //output.packed1.z  = unused

    return output;
}
// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
    float2 bary;
};

struct ShadowHitInfo
{
    bool isHit;
};

struct CameraParams
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewI;
    float4x4 projectionI;
};

//enum ShowRenderType
//{
//      INTEGRATION_RESULT = 0,
//		DIRECT_LIGHT = 1,
//		INDIRECT_LIGHT = 2,
//		DEPTH = 3,
//		NORMAL = 4,
//		AO = 5,
//      POSITION =6
//};



struct InstanceProperties
{
    float4x4 objectToWorld;
    float4x4 objectToWorldNormal;
};



#endif
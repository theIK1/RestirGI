// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.

#include "Descriptors.hlsl"

#include "frame_constants.hlsl"
#include "gbuffer.hlsl"

#include "mesh.hlsl"


SamplerState gsamLinear : register(s3);


//ConstantBuffer<CameraParams> gCameraParams : register(b0,space1);
ConstantBuffer<SceneParams> gSceneParams : register(b1, space1);


// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0,space1);
Texture2D bindless_textures[] : register(t0, space2);
StructuredBuffer<InstanceProperties> instanceProps : register(t1,space1);

Texture2D gbufferTexture : register(t2, space1);
Texture2D gNormalTexture : register(t3, space1);
Texture2D gPositionTexture : register(t4, space1);
Texture2D gMoveVector : register(t5, space1);
Texture2D gDepth : register(t6, space1);

StructuredBuffer<MeshMaterial> MeshMaterialBuffer : register(t3);










bool ShadowRayVisibility(float3 rayOrigin, float3 rayDirection, float rayMin, float rayMax)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDirection;
    ray.TMin = rayMin;
    ray.TMax = rayMax;
    
    ShadowHitInfo shadowPayload;
    shadowPayload.isHit = true;

    //trace shadowray
    TraceRay(SceneBVH,
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES
		    | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
		    | RAY_FLAG_FORCE_OPAQUE // ~skip any hit shaders
		    | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, // ~skip closest hit shaders, 
            0xFF, 1, 0,
			1, // Index of the miss shader to use in the SBT. 
			ray, shadowPayload);
    
    return shadowPayload.isHit;
}

//Direct light
float4 CalculateDirectLight(in float3 position, in float3 normal)
{
    float4 reColor = float4(0, 0, 0, 0);
    float fNDotL = 0.0;

    float3 shadowRayDirection = gSceneParams.lightPosition;

        fNDotL = dot(normal, normalize(shadowRayDirection));

        float lightPower = gSceneParams.lightPower; //lightPower
        //now we only use white light color
        float4 lightColor = float4(1.0,1.0,1.0, 0.0f); //lightColor
        float shadowFactor = ShadowRayVisibility(position, normalize(shadowRayDirection), 0.01f, 10000) ? 0.0 : 1.0;
        reColor = shadowFactor * fNDotL * lightColor * lightPower ;

  
    
    return reColor;
}

float4 TraceRadianceRay(float3 position, float3 direction)
{
    //we not use colorAndDistance.a
    PackedPayload packedPayload = (PackedPayload) 0;
    

    RayDesc rayDesc;
    rayDesc.Origin = position;
    rayDesc.Direction = direction;
    rayDesc.TMin = 0.01;
    rayDesc.TMax = 400.0;
    
    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, rayDesc, packedPayload);
    
    Payload payload = UnpackPayload(packedPayload);
    
    return float4(payload.albedo,payload.hitT);
}

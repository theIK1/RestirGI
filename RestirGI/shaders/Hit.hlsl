#include "Common.hlsl"

struct STriVertex {
	float3 position;
	float3 normal;
	float2 texCoord;
    uint meshID;
    float3 tangent;
    float3 bitangent;
    float4 color;
    uint materialID;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
Texture2D<float4> tex : register(t2);

[shader("closesthit")]void ClosestHit(inout PackedPayload packedPayload, Attributes attrib)//PackedPayload
{
    if (gSceneParams.showRenderType == 7 || 1)
    {
        //ÁíÒ»¸ö°æ±¾
        
        {
            Payload payload = (Payload) 0;
            payload.hitT = RayTCurrent();
        
            float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
            uint vertId = 3 * PrimitiveIndex();
            float2 hitTexCoord = BTriVertex[indices[vertId + 0]].texCoord * barycentrics.x +
						 BTriVertex[indices[vertId + 1]].texCoord * barycentrics.y +
						 BTriVertex[indices[vertId + 2]].texCoord * barycentrics.z;
            float3 hitNormal = BTriVertex[indices[vertId + 0]].normal * barycentrics.x +
						BTriVertex[indices[vertId + 1]].normal * barycentrics.y +
						BTriVertex[indices[vertId + 2]].normal * barycentrics.z;
            uint mesh_id = BTriVertex[indices[vertId + 0]].meshID;
            float4 textureColor;
            
            if (gSceneParams.patch1 > 0)
            {
                if (MeshMaterialBuffer[mesh_id].albedo_map < 200)
                    textureColor = bindless_textures[NonUniformResourceIndex(MeshMaterialBuffer[mesh_id].albedo_map)].SampleLevel(gsamLinear, hitTexCoord, 0.0);
                else
                    textureColor = float4(1.0, 1.0, 1.0, 1.0);

            }
            else
                textureColor = tex.SampleLevel(gsamLinear, hitTexCoord, 0.0);
            
            hitNormal = normalize(hitNormal);
            float3 hitOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
        
            float4 color = CalculateDirectLight(hitOrigin, hitNormal);
            payload.albedo = float3(1, 1, 1);//kd 
            payload.albedo *= color * textureColor;
            
        
            
            
            packedPayload = PackPayload(payload);
        
        
            return;
        }

        Payload payload = (Payload) 0;
        payload.hitT = RayTCurrent();
        payload.hitKind = HitKind();
        
        float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
        uint vertId = 3 * PrimitiveIndex();
	
        //World position
        float3 hitWorldPosition = BTriVertex[indices[vertId + 0]].position * barycentrics.x +
						 BTriVertex[indices[vertId + 1]].position * barycentrics.y +
						 BTriVertex[indices[vertId + 2]].position * barycentrics.z;
        payload.worldPosition = hitWorldPosition;
        payload.worldPosition = mul(ObjectToWorld3x4(), float4(payload.worldPosition, 1.f)).xyz;
        


        //normal
        float3 hitNormal = BTriVertex[indices[vertId + 0]].normal * barycentrics.x +
						BTriVertex[indices[vertId + 1]].normal * barycentrics.y +
						BTriVertex[indices[vertId + 2]].normal * barycentrics.z;
        
        payload.normal = hitNormal;
        payload.normal = normalize(mul(ObjectToWorld3x4(), float4(payload.normal, 0.f)).xyz);
        payload.shadingNormal = payload.normal;
        
        //albedo and opacity
        float2 hitTexCoord = BTriVertex[indices[vertId + 0]].texCoord * barycentrics.x +
						 BTriVertex[indices[vertId + 1]].texCoord * barycentrics.y +
						 BTriVertex[indices[vertId + 2]].texCoord * barycentrics.z;
        
        float4 bco = tex.SampleLevel(gsamLinear, hitTexCoord, 0.0);
        payload.albedo = bco.rgb;
        payload.opacity = bco.a;


    
    // Pack the payload
        packedPayload = PackPayload(payload);
        
        
        return;
    }
        
    //deferred rendering
    if (gSceneParams.showRenderMode == 1)
    {

        

        
        
        return;

    }

}


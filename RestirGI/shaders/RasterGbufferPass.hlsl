
#include "gbuffer.hlsl"
#include "frame_constants.hlsl"

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    
    float4 worldSpacePosition : WORLDPOSITION;
    float4 viewSpacePosition : VIEWPOSITION;
    float4 projSpacePosition : PROJECTIONPOSITION;
    
    float4 lastviewSpacePosition : LASTVIEWPOSITION;
    float4 lastprojSpacePosition : LASTPROJECTIONPOSITION;
    
    uint meshID : MESHID;
    float4 color : COLOR;
};

struct PSOutput
{
    float4 gbuffer : SV_TARGET0;
    float3 normal : SV_TARGET1;
    float4 worldPosition : SV_TARGET2;
    float4 moveVector : SV_TARGET3;
};

cbuffer ObjectConstant : register(b1)
{
    float4x4 world;
}


/*
cbuffer CameraParams : register(b0)
{
    float4x4 view;
    float4x4 projection;

}
*/

struct VSInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    uint meshID : MESHID;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float4 color : COLOR;
};


Texture2D gDiffuseMap : register(t0);
SamplerState sampler_llr : register(s1);

PSInput VSMain(VSInput input)
{
    PSInput result;
    
    float4 pos = float4(input.position.xyz, 1);
    float4 lastPos = float4(input.position.xyz, 1);

    pos = mul(world, pos);
    lastPos = pos;
    result.worldSpacePosition = pos;
    pos = mul(frame_constants.view_constants.world_to_view, pos);
    lastPos = mul(frame_constants.view_constants.prev_world_to_prev_view, lastPos);
    result.viewSpacePosition = pos;
    result.lastviewSpacePosition = lastPos;
    
    pos = mul(frame_constants.view_constants.view_to_sample, pos);
    lastPos = mul(frame_constants.view_constants.prev_view_to_prev_clip, lastPos);
    result.projSpacePosition = pos;
    result.lastprojSpacePosition = lastPos;
    
    
    
    result.position = pos;
    result.normal = input.normal;
    result.texCoord = input.texCoord;
    result.meshID = input.meshID;
    
    result.color = input.color;
       
    return result;
}

PSOutput PSMain(PSInput input) : SV_TARGET
{	
    float viewSpaceDepth = input.viewSpacePosition.z;  
    
    float projSpaceDepth = input.projSpacePosition.z;    
    float derivativeDepth = max(abs(ddx(projSpaceDepth)), abs(ddy(projSpaceDepth)));
    
    PSOutput output;
    float4 albedo = gDiffuseMap.SampleLevel(sampler_llr, input.texCoord, 0) * input.color;
 
    
    if (albedo.a < 0.01)
        discard;
    albedo.rgb *= albedo.a;
    albedo.a = viewSpaceDepth;
    
    
    float3 normal_ws = input.normal;
    //output.normal = float4(input.normal, input.meshID);
    output.worldPosition = float4(input.worldSpacePosition.xyz, derivativeDepth);
    
    float lastDepth = input.lastviewSpacePosition.z;
    float lastProjDepth = input.lastprojSpacePosition.z;
    float2 screenPos = input.projSpacePosition.xy / input.projSpacePosition.w; //[-1,1]
    float2 lastScreenPos = input.lastprojSpacePosition.xy / input.lastprojSpacePosition.w;
    float derivativeDepth0 = max(ddx(lastDepth), ddy(lastDepth));
    
    output.moveVector = float4(lastScreenPos - screenPos, lastDepth, lastProjDepth);
    
    screenPos.xy += 1.0;
    screenPos.xy /= 2.0;
    screenPos.y *= -1.0; //[0,1] [-1,0]
    /* 取值大致如下(对于一张纹理而言)
     (0,-1)    (0.5,-1)    (1,-1)
     (0,-0.5)  (0.5,-0.5)  (1,-0.5)
     (0,0)     (0.5,0)     (1,0)
    */

    lastScreenPos.xy += 1.0;
    lastScreenPos.xy /= 2.0;
    lastScreenPos.y *= -1.0;
    
    //output.moveVector = float4(screenPos, lastDepth, lastProjDepth);
    
    //Derive normal from depth
    float3 d1 = ddx(input.viewSpacePosition);
    float3 d2 = ddy(input.viewSpacePosition);
    float3 geometric_normal_vs = normalize(cross(d2, d1));
    float3 geometric_normal_ws = mul(frame_constants.view_constants.view_to_world, float4(geometric_normal_vs, 0));
    
    // Fix invalid normals
    if (dot(input.normal, geometric_normal_ws) < 0.0)
    {
        normal_ws *= -1;
    }
    
    //gbuffer
    GbufferData gbuffer = GbufferData::create_zero();
    gbuffer.albedo = albedo.rgb;
    gbuffer.normal = normal_ws;
    gbuffer.roughness = 1.0;
    

    float4 vs_pos = input.viewSpacePosition / input.viewSpacePosition.w;
    float4 prev_vs_pos = input.lastviewSpacePosition / input.lastviewSpacePosition.w;
    
    
    output.gbuffer = asfloat(gbuffer.pack().data0); //asfloat(gbuffer.pack().data0)
    output.normal = geometric_normal_vs * 0.5 + 0.5; 
    output.moveVector = float4(0,0,0, 0); //prev_vs_pos.xyz - vs_pos.xyz

    
    

    
    return output;
}

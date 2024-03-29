#include "frame_constants.hlsl"

#include "gbuffer.hlsl"

Texture2D<float4> input_tex : register(t0);


RWTexture2D<float4> output_tex : register(u0);


float3 normal_ws_at_px(int2 px)
{
    return unpack_normal_11_10_11_no_normalize(input_tex[px].y);
}


[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    int2 px = DispatchThreadId.xy;

    const int2 src_px = px * 2 + HALFRES_SUBSAMPLE_OFFSET;

    float3 normal_ws = 1;

    // wired
    {
        const int k = 1;

        float3 avg_normal = 0;
        {
            for (int y = -k; y <= k + 1; ++y)
            {
                for (int x = -k; x <= k + 1; ++x)
                {
                    avg_normal += normal_ws_at_px(px * 2 + int2(x, y));
                }
            }
        }
        avg_normal = normalize(avg_normal);

        float lowest_dot = 10;
        {
            for (int y = -k + 1; y <= k; ++y)
            {
                for (int x = -k + 1; x <= k; ++x)
                {
                    float3 normal = normal_ws_at_px(px * 2 + int2(x, y));
                    float d = dot(normal, avg_normal);
                    if (d < lowest_dot)
                    {
                        lowest_dot = d;
                        normal_ws = normal;
                    }
                }
            }
        }
    }
    
    // tired; TODO: the "wired" version upsets specular, which assumes
    // that normals match up with hi-res ones at hi px locations
    normal_ws = normal_ws_at_px(src_px);

    float3 normal_vs = normalize(mul(frame_constants.view_constants.world_to_view, float4(normal_ws, 0)).xyz);
    output_tex[px] = float4(normal_vs, 1);
   

}
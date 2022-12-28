#include "frame_constants.hlsl"
#include "bilinear.hlsl"
#include "gbuffer.hlsl"
#include "samplers.hlsl"

Texture2D<float3> Normal : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float3> Velocity : register(t2);
Texture2D<float> PrevDepth : register(t3);

RWTexture2D<float4> RWreprojectionMap : register(u0);
RWTexture2D<float4> RWTest : register(u1);





static const float4 output_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);

[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    //测试
    {
        const float2 uv = get_uv(DispatchThreadId.xy, output_tex_size);
        const ViewRayContext view_ray_context = ViewRayContext::from_uv_and_biased_depth(uv, Depth[DispatchThreadId.xy]);
        
       // RWTest[DispatchThreadId.xy] = float4(view_ray_context.ray_dir_ws_h);

    }
    
    uint2 px = DispatchThreadId.xy;
    
    float2 uv = get_uv(px, output_tex_size);

    if (Depth[px] == 0.0)
    {
        float4 pos_cs = float4(uv_to_cs(uv), 0.0, 1.0);
        float4 pos_vs = mul(frame_constants.view_constants.clip_to_view, pos_cs);

        float4 prev_vs = pos_vs;
        
        float4 prev_cs = mul(frame_constants.view_constants.view_to_clip, prev_vs);
        float4 prev_pcs = mul(frame_constants.view_constants.clip_to_prev_clip, prev_cs);

        float2 prev_uv = cs_to_uv(prev_pcs.xy);
        float2 uv_diff = prev_uv - uv;

        RWreprojectionMap[px] = float4(uv_diff, 0, 0);
        
        return;
    }
    

    float3 eye_pos = mul(frame_constants.view_constants.view_to_world, float4(0, 0, 0, 1)).xyz; //摄像机的世界位置

    float depth = 0.0;
    {
        const int k = 0;
        for (int y = -k; y <= k; ++y)
        {
            for (int x = -k; x <= k; ++x)
            {
                float s_depth = Depth[px + int2(x, y)];
                if (s_depth != 0.0)
                {
                    depth = max(depth, s_depth);  //将深度的下限设置为0. 有点奇怪，正常输入的深度值应该不可能为0以下，所以应该没啥用
                }
            }
        }
    }

    float3 normal_vs = Normal[px] * 2.0 - 1.0;
    float3 normal_pvs = mul(frame_constants.view_constants.prev_clip_to_prev_view,
        mul(frame_constants.view_constants.clip_to_prev_clip,
            mul(frame_constants.view_constants.view_to_clip, float4(normal_vs, 0)))).xyz;

    float4 pos_cs = float4(uv_to_cs(uv), depth, 1.0); //屏幕上像素点的位置
    float4 pos_vs = mul(frame_constants.view_constants.clip_to_view, pos_cs);
    float dist_to_point = -(pos_vs.z / pos_vs.w); //相机到屏幕上点的距离

    {
        float4 temp = float4(4, 3, 2, 1);
        RWTest[DispatchThreadId.xy] = mul(frame_constants.view_constants.clip_to_view,temp);

    }
    
    
    float4 prev_vs = pos_vs / pos_vs.w;
    prev_vs.xyz += float4(Velocity[px].xyz, 0).xyz;
    
    
    
    //float4 prev_cs = mul(frame_constants.view_constants.prev_view_to_prev_clip, prev_vs);
    float4 prev_cs = mul(frame_constants.view_constants.view_to_clip, prev_vs);
    float4 prev_pcs = mul(frame_constants.view_constants.clip_to_prev_clip, prev_cs);


    
    float2 prev_uv = cs_to_uv(prev_pcs.xy / prev_pcs.w);
    float2 uv_diff = prev_uv - uv;
    
    

    // Account for quantization of the `uv_diff` in R16G16B16A16_SNORM.
    // This is so we calculate validity masks for pixels that the users will actually be using.
    uv_diff = floor(uv_diff * 32767.0 + 0.5) / 32767.0;
    prev_uv = uv + uv_diff;

    float4 prev_pvs = mul(frame_constants.view_constants.prev_clip_to_prev_view, prev_pcs);

    prev_pvs /= prev_pvs.w;

    // Based on "Fast Denoising with Self Stabilizing Recurrent Blurs"
    
    float plane_dist_prev = dot(normal_vs, prev_pvs.xyz);

    // 与引用的技术不同：他们通过使用视图空间Z线性缩放平面距离来计算重投影的样本深度，除非平面与视图对齐，否则这是不正确的
    // 相反，距离实际随深度增加的量仅为“normal_vs.z”

    // Note: bias the minimum distance increase, so that reprojection at grazing angles has a sharper cutoff.
    // This can introduce shimmering a grazing angles, but also reduces reprojection artifacts on surfaces
    // which flip their normal from back- to fron-facing across a frame. Such reprojection smears a few
    // pixels along a wide area, creating a glitchy look.
    float plane_dist_prev_dz = min(-0.2, normal_vs.z);
    //float plane_dist_prev_dz = -normal_vs.z;

    const Bilinear bilinear_at_prev = get_bilinear_filter(prev_uv, output_tex_size.xy);
    float2 prev_gather_uv = (bilinear_at_prev.origin + 1.0) / output_tex_size.xy;
    float4 prev_depth = PrevDepth.GatherRed(sampler_nnc, prev_gather_uv).wzxy;

    float4 prev_view_z = rcp(prev_depth * -frame_constants.view_constants.prev_clip_to_prev_view._43);

    // Note: departure from the quoted technique: linear offset from zero distance at previous position instead of scaling.
    float4 quad_dists = abs(plane_dist_prev_dz * (prev_view_z - prev_pvs.z));

    
    // TODO: reject based on normal too? Potentially tricky under rotations.

    // Resolution-dependent. Was tweaked for 1080p
    const float acceptance_threshold = 0.001 * (1080 / output_tex_size.y);

    // Reduce strictness at grazing angles, where distances grow due to perspective
    const float3 pos_vs_norm = normalize(pos_vs.xyz / pos_vs.w); //从相机看向屏幕像素中点的向量
    const float ndotv = dot(normal_vs, pos_vs_norm);
    const float prev_ndotv = dot(normal_pvs, normalize(prev_pvs.xyz));

    
    
    float4 quad_validity = step(quad_dists, acceptance_threshold * dist_to_point / -ndotv);
    
    

    quad_validity.x *= all(bilinear_at_prev.px0() >= 0) && all(bilinear_at_prev.px0() < int2(output_tex_size.xy));
    quad_validity.y *= all(bilinear_at_prev.px1() >= 0) && all(bilinear_at_prev.px1() < int2(output_tex_size.xy));
    quad_validity.z *= all(bilinear_at_prev.px2() >= 0) && all(bilinear_at_prev.px2() < int2(output_tex_size.xy));
    quad_validity.w *= all(bilinear_at_prev.px3() >= 0) && all(bilinear_at_prev.px3() < int2(output_tex_size.xy));

    
    
    float validity = dot(quad_validity, float4(1, 2, 4, 8)) / 15.0;

    
    
    float2 texel_center_offset = abs(0.5 - frac(prev_uv * output_tex_size.xy));

    float accuracy = 1;
    
    // Reprojection of surfaces which were grazing to the camera
    // causes any noise or features on those surfaces to become smeared,
    // which subsequently is slow to converge.
    accuracy *= smoothstep(0.8, 0.95, prev_ndotv / ndotv);

    // Mark off-screen reprojections
    if (any(saturate(prev_uv) != prev_uv))
    {
        accuracy = -1;
    }


    //RWreprojectionMap[px] = float4(0, 0, 0, 0);
    RWreprojectionMap[px] = float4(uv_diff, validity, accuracy);
   

}
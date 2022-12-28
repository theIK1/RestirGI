#include "samplers.hlsl"
#include "uv.hlsl"
#include "frame_constants.hlsl"
#include "bindless_textures.hlsl"
#include "luminance_histogram_common.hlsl"

#define DECLARE_BEZOLD_BRUCKE_LUT
static float2 SAMPLE_BEZOLD_BRUCKE_LUT(float coord) {
    return bindless_textures[BINDLESS_LUT_BEZOLD_BRUCKE].SampleLevel(sampler_llr, float2(coord, 0.5), 0).xy;
}
#include "display_transform.hlsl"

Texture2D<float4> input_tex : register(t0);

RWTexture2D<float4> output_tex : register(u0);


static const float4 output_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);

#define USE_GRADE 0
#define USE_DISPLAY_TRANSFORM 1
#define USE_DITHER 1
#define USE_SHARPEN 0
#define USE_VIGNETTE 1

#define DEBUG_HISTOGRAM 0

static const float sharpen_amount = 0.1;
static const float glare_amount = 0.05;
//static const float glare_amount = 0.0;

float sharpen_remap(float l) {
    return sqrt(l);
}

float sharpen_inv_remap(float l) {
    return l * l;
}

float triangle_remap(float n) {
    float origin = n * 2.0 - 1.0;
    float v = origin * rsqrt(abs(origin));
    v = max(-1.0, v);
    v -= sign(origin);
    return v;
}

// (Very) reduced version of:
// Uchimura 2017, "HDR theory and practice"
// Math: https://www.desmos.com/calculator/gslcdxvipg
// Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
//  m: linear section start
//  c: black
float3 push_down_black_point(float3 x, float m, float c) {
    float3 w0 = 1.0 - smoothstep(0.0, m, x);
    float3 w1 = 1.0 - w0;

    float3 T = m * pow(x / m, c);
    float3 L = x;

    return T * w0 + L * w1;
}

groupshared uint max_histogram_bin;


[numthreads(8, 8, 1)]
void CS(uint2 px: SV_DispatchThreadID, uint idx_within_group: SV_GroupIndex) {
    float2 uv = get_uv(px, output_tex_size);

#if 0
    output_tex[px] = input_tex[px];
    return;
#endif

    
    float3 col = input_tex[px].rgb;
    
    col = col / (col + float3(1.0,1.0,1.0));
    
    if ((frame_constants.size.pad0 & (1 << 4)) != (1 << 4))
    {
        output_tex[px] = float4(col, 1);
        return;
    }

    output_tex[px] = float4(col, 1);
    return;
    
#if USE_SHARPEN
	float neighbors = 0;
	float wt_sum = 0;

	const int2 dim_offsets[] = { int2(1, 0), int2(0, 1) };

	float center = sharpen_remap(sRGB_to_luminance(col.rgb));
    float2 wts;

	for (int dim = 0; dim < 2; ++dim) {
		int2 n0coord = px + dim_offsets[dim];
		int2 n1coord = px - dim_offsets[dim];

		float n0 = sharpen_remap(sRGB_to_luminance(input_tex[n0coord].rgb));
		float n1 = sharpen_remap(sRGB_to_luminance(input_tex[n1coord].rgb));
		float wt = max(0, 1.0 - 6.0 * (abs(center - n0) + abs(center - n1)));
        wt = min(wt, sharpen_amount * wt * 1.25);
        
		neighbors += n0 * wt;
		neighbors += n1 * wt;
		wt_sum += wt * 2;
	}

    float sharpened_luma = max(0, center * (wt_sum + 1) - neighbors);
    sharpened_luma = sharpen_inv_remap(sharpened_luma);

	col.rgb *= max(0.0, sharpened_luma / max(1e-5, sRGB_to_luminance(col.rgb)));
#endif

    
        col = max(0.0, col);
    //col = col * (1.0 - debug_input_tex[px].a) + debug_input_tex[px].rgb;

   

#if USE_VIGNETTE
    col *= exp(-2 * pow(length(uv - 0.5), 3));
#endif

#if USE_GRADE
    // Lift mids
    col = pow(col, 0.9);

    // Push down lows
    col = push_down_black_point(col, 0.2, 1.25);
#endif

#if USE_DISPLAY_TRANSFORM
    // Apply a perceptually neutral display transform
    col = display_transform_sRGB(col);
#endif


    // Dither
#if USE_DITHER
    const uint urand_idx = frame_constants.frame_index;
    // 256x256 blue noise
    float dither = triangle_remap(bindless_textures[BINDLESS_LUT_BLUE_NOISE_256_LDR_RGBA_0][
        (px + int2(urand_idx * 59, urand_idx * 37)) & 255
    ].x);

    col += dither / 256.0;
#endif



    output_tex[px] = float4(col, 1);
}

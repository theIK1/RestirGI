#ifndef BINDLESS_TEXTURES_HLSL
#define BINDLESS_TEXTURES_HLSL

Texture2D bindless_textures[] : register(t1, space2);

// Pre-integrated FG texture for the GGX BRDF
static const uint BINDLESS_LUT_BRDF_FG = 198;

static const uint BINDLESS_LUT_BLUE_NOISE_256_LDR_RGBA_0 = 199; //1

static const uint BINDLESS_LUT_BEZOLD_BRUCKE = 2;

#endif

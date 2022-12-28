#include "uv.hlsl"
#include "pack_unpack.hlsl"
#include "frame_constants.hlsl"
#include "gbuffer.hlsl"
#include "brdf.hlsl"
#include "brdf_lut.hlsl"
#include "layered_brdf.hlsl"
#include "rt.hlsl"
#include "quasi_random.hlsl"
#include "bindless_textures.hlsl"
#include "atmosphere.hlsl"
#include "sun.hlsl"
#include "triangle.hlsl"
#include "BVH.hlsl"

TextureCube<float4> sky_cube_tex : register(t2);

RWTexture2D<float4> output_tex : register(u0);

// Does not include the segment used to connect to the sun
static const uint MAX_EYE_PATH_LENGTH = 16;

static const uint RUSSIAN_ROULETTE_START_PATH_LENGTH = 3;
static const float MAX_RAY_LENGTH = FLT_MAX;
//static const float MAX_RAY_LENGTH = 5.0;

// Rough-smooth-rough specular paths are a major source of fireflies.
// Enabling this option will bias roughness of path vertices following
// reflections off rough interfaces.
static const bool FIREFLY_SUPPRESSION = true;
static const bool FURNACE_TEST = !true;
static const bool FURNACE_TEST_EXCLUDE_DIFFUSE = !true;
static const bool USE_PIXEL_FILTER = true;
static const bool INDIRECT_ONLY = !true;
static const bool GREY_ALBEDO_FIRST_BOUNCE = !true;
static const bool BLACK_ALBEDO_FIRST_BOUNCE = !true;
static const bool ONLY_SPECULAR_FIRST_BOUNCE = !true;
static const bool USE_SOFT_SHADOWS = true;
static const bool SHOW_ALBEDO = !true;

static const bool USE_LIGHTS = true;
static const bool USE_EMISSIVE = true;
static const bool RESET_ACCUMULATION = !true;
static const bool ROLLING_ACCUMULATION = !true;

float3 sample_environment_light(float3 dir) {
    //return 0.5.xxx;

    if (FURNACE_TEST) {
        return 0.5.xxx;
    }
    return sky_cube_tex.SampleLevel(sampler_llr, dir, 0).rgb;
    return atmosphere_default(dir, SUN_DIRECTION);

    float3 col = (dir.zyx * float3(1, 1, -1) * 0.5 + float3(0.6, 0.5, 0.5)) * 0.75;
    col = lerp(col, 1.3.xxx * sRGB_to_luminance(col), smoothstep(-0.2, 1.0, dir.y).xxx);
    return col;
}

// Approximate Gaussian remap
// https://www.shadertoy.com/view/MlVSzw
float inv_error_function(float x, float truncation) {
    static const float ALPHA = 0.14;
    static const float INV_ALPHA = 1.0 / ALPHA;
    static const float K = 2.0 / (M_PI * ALPHA);

	float y = log(max(truncation, 1.0 - x*x));
	float z = K + 0.5 * y;
	return sqrt(max(0.0, sqrt(z*z - y * INV_ALPHA) - z)) * sign(x);
}

float remap_unorm_to_gaussian(float x, float truncation) {
	return inv_error_function(x * 2.0 - 1.0, truncation);
}

[shader("raygeneration")]
void PathTraceRgen()
{
    const uint2 px = DispatchRaysIndex().xy;

    float4 prev;
    if (ROLLING_ACCUMULATION) {
        prev = float4(output_tex[px].rgb, 8);
    } else {
        prev = RESET_ACCUMULATION ? 0 : output_tex[px];
    }

    if (prev.w < 1000)
    {
        float4 radiance_sample_count_packed = 0.0;
        uint rng = hash_combine2(hash_combine2(px.x, hash1(px.y)), frame_constants.frame_index);

        static const uint sample_count = 1;
        for (uint sample_idx = 0; sample_idx < sample_count; ++sample_idx) {
            float px_off0 = 0.5;
            float px_off1 = 0.5;

            if (USE_PIXEL_FILTER) {
                const float psf_scale = 0.4;
                px_off0 += psf_scale * remap_unorm_to_gaussian(uint_to_u01_float(hash1_mut(rng)), 1e-8);
                px_off1 += psf_scale * remap_unorm_to_gaussian(uint_to_u01_float(hash1_mut(rng)), 1e-8);
            }

            const float2 pixel_center = px + float2(px_off0, px_off1);
            const float2 uv = pixel_center / DispatchRaysDimensions().xy;

            RayDesc outgoing_ray;
            {
                const ViewRayContext view_ray_context = ViewRayContext::from_uv(uv);
                const float3 ray_dir_ws = view_ray_context.ray_dir_ws();

                outgoing_ray = new_ray(
                    view_ray_context.ray_origin_ws(), 
                    normalize(ray_dir_ws.xyz),
                    0.0,
                    FLT_MAX
                );
            }

            float3 throughput = 1.0.xxx;
            float3 total_radiance = 0.0.xxx;

            float roughness_bias = 0.0;

            RayCone ray_cone = pixel_ray_cone_from_image_height(
                DispatchRaysDimensions().y
            );

            // Bias for texture sharpness
            ray_cone.spread_angle *= 0.3;

            [loop]
            for (uint path_length = 0; path_length < MAX_EYE_PATH_LENGTH; ++path_length) {
                /*if (path_length == 1 && outgoing_ray.Direction.x > -0.8) {
                    throughput = 0;
                } else {
                    throughput *= 2;
                }*/

                if (path_length == 1) {
                    outgoing_ray.TMax = MAX_RAY_LENGTH;
                }

                GbufferPathVertex primary_hit = GbufferRaytrace::with_ray(outgoing_ray)
                    .with_cone(ray_cone)
                    //.with_cull_back_faces(true || 0 == path_length)
                    .with_cull_back_faces(false)
                    .with_path_length(path_length)
                    .trace(acceleration_structure);

                if (primary_hit.is_hit) {
                    // TODO
                    const float surface_spread_angle = 0.0;
                    ray_cone = ray_cone.propagate(surface_spread_angle, primary_hit.ray_t);

                    const float3 to_light_norm = sample_sun_direction(
                        float2(uint_to_u01_float(hash1_mut(rng)), uint_to_u01_float(hash1_mut(rng))),
                        true
                    );
                    
                    const bool is_shadowed =
                        (INDIRECT_ONLY && path_length == 0) ||
                        rt_is_shadowed(
                            acceleration_structure,
                            new_ray(
                                primary_hit.position,
                                to_light_norm,
                                1e-4,
                                FLT_MAX
                        ));

                    GbufferData gbuffer = unpack(primary_hit.gbuffer_packed);


                    if (SHOW_ALBEDO) {
                        output_tex[px] = float4(gbuffer.albedo, 1);
                        return;
                    }

                    if (dot(gbuffer.normal, outgoing_ray.Direction) >= 0.0) {
                        if (0 == path_length) {
                            // Flip the normal for primary hits so we don't see blackness
                            gbuffer.normal = -gbuffer.normal;
                        } else {
                            break;
                        }
                    }

                    if (FURNACE_TEST && !FURNACE_TEST_EXCLUDE_DIFFUSE) {
                        gbuffer.albedo = 1;
                    }

                    //gbuffer.albedo = float3(0.966653, 0.802156, 0.323968); // Au from Mitsuba
                    //gbuffer.albedo = 0;
                    //gbuffer.metalness = 1.0;
                    //gbuffer.roughness = 0.5;//lerp(gbuffer.roughness, 1.0, 0.8);

                    if (INDIRECT_ONLY && path_length == 0) {
                        gbuffer.albedo = 1.0;
                        gbuffer.metalness = 0.0;
                    }

                    // For reflection comparison against RTR
                    /*if (path_length == 0) {
                        gbuffer.albedo = 0;
                    }*/

                    if (ONLY_SPECULAR_FIRST_BOUNCE && path_length == 0) {
                        gbuffer.albedo = 1.0;
                        gbuffer.metalness = 1.0;
                        //gbuffer.roughness = 0.01;
                    }

                    if (GREY_ALBEDO_FIRST_BOUNCE && path_length == 0) {
                        gbuffer.albedo = 0.5;
                    }
                    
                    if (BLACK_ALBEDO_FIRST_BOUNCE && path_length == 0) {
                        gbuffer.albedo = 0.0;
                    }

                    //gbuffer.roughness = lerp(gbuffer.roughness, 0.0, 0.8);
                    //gbuffer.metalness = 1.0;
                    //gbuffer.albedo = max(gbuffer.albedo, 1e-3);
                    //gbuffer.roughness = 0.07;
                    //gbuffer.roughness = clamp((int(primary_hit.position.x * 0.2) % 5) / 5.0, 1e-4, 1.0);

                    const float3x3 tangent_to_world = build_orthonormal_basis(gbuffer.normal);
                    const float3 wi = mul(to_light_norm, tangent_to_world);

                    float3 wo = mul(-outgoing_ray.Direction, tangent_to_world);

                    // Hack for shading normals facing away from the outgoing ray's direction:
                    // We flip the outgoing ray along the shading normal, so that the reflection's curvature
                    // continues, albeit at a lower rate.
                    if (wo.z < 0.0) {
                        wo.z *= -0.25;
                        wo = normalize(wo);
                    }

                    LayeredBrdf brdf = LayeredBrdf::from_gbuffer_ndotv(gbuffer, wo.z);

                    if (FIREFLY_SUPPRESSION) {
                        brdf.specular_brdf.roughness = lerp(brdf.specular_brdf.roughness, 1.0, roughness_bias);
                    }

                    if (FURNACE_TEST && FURNACE_TEST_EXCLUDE_DIFFUSE) {
                        brdf.diffuse_brdf.albedo = 0.0.xxx;
                    }

                    if (!FURNACE_TEST && !(ONLY_SPECULAR_FIRST_BOUNCE && path_length == 0)) {
                        const float3 brdf_value = brdf.evaluate_directional_light(wo, wi);
                        const float3 light_radiance = is_shadowed ? 0.0 : SUN_COLOR;
                        total_radiance += throughput * brdf_value * light_radiance * max(0.0, wi.z);

                        if (USE_EMISSIVE) {
                            total_radiance += gbuffer.emissive * throughput;
                        }
                        
                        
                    }

                    float3 urand;
                    BrdfSample brdf_sample = BrdfSample::invalid();

                    #if 0
                    if (path_length == 0) {
                        const uint noise_offset = frame_constants.frame_index;

                        urand = bindless_textures[BINDLESS_LUT_BLUE_NOISE_256_LDR_RGBA_0][
                            (px + int2(noise_offset * 59, noise_offset * 37)) & 255
                        ].xyz * 255.0 / 256.0 + 0.5 / 256.0;

                        urand.x += uint_to_u01_float(hash1(frame_constants.frame_index));
                        urand.y += uint_to_u01_float(hash1(frame_constants.frame_index + 103770841));
                        urand.z += uint_to_u01_float(hash1(frame_constants.frame_index + 828315679));

                        urand = frac(urand);
                    } else
                    #endif
                    {
                        urand = float3(
                            uint_to_u01_float(hash1_mut(rng)),
                            uint_to_u01_float(hash1_mut(rng)),
                            uint_to_u01_float(hash1_mut(rng)));
                    }

                    brdf_sample = brdf.sample(wo, urand);

                    if (brdf_sample.is_valid()) {
                        if (FIREFLY_SUPPRESSION) {
                            roughness_bias = lerp(roughness_bias, 1.0, 0.5 * brdf_sample.approx_roughness);
                        }

                        outgoing_ray.Origin = primary_hit.position;
                        outgoing_ray.Direction = mul(tangent_to_world, brdf_sample.wi);
                        outgoing_ray.TMin = 1e-4;
                        throughput *= brdf_sample.value_over_pdf;
                    } else {
                         break;
                    }

                    if (FURNACE_TEST) {
                        // Short-circuit the path tracing
                        total_radiance += throughput * sample_environment_light(outgoing_ray.Direction);
                        break;
                    }

                    // Russian roulette
                    if (path_length >= RUSSIAN_ROULETTE_START_PATH_LENGTH) {
                        const float rr_coin = uint_to_u01_float(hash1_mut(rng));
                        const float continue_p = max(gbuffer.albedo.r, max(gbuffer.albedo.g, gbuffer.albedo.b));
                        if (rr_coin > continue_p) {
                            break;
                        } else {
                            throughput /= continue_p;
                        }
                    }
                } else {
                    total_radiance += throughput * sample_environment_light(outgoing_ray.Direction);
                    break;
                }
            }

            if (all(total_radiance >= 0.0)) {
                radiance_sample_count_packed += float4(total_radiance, 1.0);
            }
        }

        float4 cur = radiance_sample_count_packed;

        float tsc = cur.w + prev.w;
        float lrp = cur.w / max(1.0, tsc);
        cur.rgb /= max(1.0, cur.w);

        output_tex[px] = float4(max(0.0.xxx, lerp(prev.rgb, cur.rgb, lrp)), max(1, tsc));
    }
}

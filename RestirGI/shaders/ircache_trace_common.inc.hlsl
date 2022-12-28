#ifndef IRRCACHE_TRACE_COMMOM_INC_HLSL
#define IRRCACHE_TRACE_COMMOM_INC_HLSL
// HACK: reduces feedback loops due to the spherical traces.
// As a side effect, dims down the result a bit, and increases variance.
// Maybe not needed when using IRCACHE_LOOKUP_PRECISE.
#define USE_SELF_LIGHTING_LIMITER 1

#define USE_WORLD_RADIANCE_CACHE 0

#define USE_BLEND_RESULT 0

// Rough-smooth-rough specular paths are a major source of fireflies.
// Enabling this option will bias roughness of path vertices following
// reflections off rough interfaces.
static const bool FIREFLY_SUPPRESSION = true;
static const bool USE_LIGHTS = false;   //这里原本为true
static const bool USE_EMISSIVE = true;
static const bool SAMPLE_IRCACHE_AT_LAST_VERTEX = true;
static const uint MAX_PATH_LENGTH = 1;

float3 sample_environment_light(float3 dir) {
    //return sky_cube_tex.SampleLevel(sampler_llr, dir, 0).rgb;
    return float3(1.0, 1.0, 1.0);

}

float pack_dist(float x) {
    return min(1, x);
}

float unpack_dist(float x) {
    return x;
}

struct IrcacheTraceResult {
    float3 incident_radiance;
    float3 direction;
    float3 hit_pos;
};

IrcacheTraceResult ircache_trace(Vertex entry, DiffuseBrdf brdf, SampleParams sample_params, uint life) {
    const float3x3 tangent_to_world = build_orthonormal_basis(entry.normal);

    uint rng = sample_params.rng();

    RayDesc outgoing_ray = new_ray(
        entry.position,
        sample_params.direction(),
        0.0,
        FLT_MAX
    );

    // force rays in the direction of the normal (debug)
    //outgoing_ray.Direction = mul(tangent_to_world, float3(0, 0, 1));

    IrcacheTraceResult result;
    result.direction = outgoing_ray.Direction;

    #if USE_WORLD_RADIANCE_CACHE
        WrcFarField far_field =
            WrcFarFieldQuery::from_ray(outgoing_ray.Origin, outgoing_ray.Direction)
                .with_interpolation_urand(float3(
                    uint_to_u01_float(hash1_mut(rng)),
                    uint_to_u01_float(hash1_mut(rng)),
                    uint_to_u01_float(hash1_mut(rng))
                ))
                .with_query_normal(entry.normal)
                .query();
    #else
        WrcFarField far_field = WrcFarField::create_miss();
    #endif

    if (far_field.is_hit()) {
        outgoing_ray.TMax = far_field.probe_t;
    }

    // ----

    float3 throughput = 1.0.xxx;
    float roughness_bias = 0.5;

    float3 irradiance_sum = 0;
    float2 hit_dist_wt = 0;

    for (uint path_length = 0; path_length < MAX_PATH_LENGTH; ++path_length)
    {
        const GbufferPathVertex primary_hit = GbufferRaytrace::with_ray(outgoing_ray)
            .with_cone(RayCone::from_spread_angle(0.1))
            .with_cull_back_faces(false)
            .with_path_length(path_length + 1) // +1 because this is indirect light
            .trace(acceleration_structure);

        if (primary_hit.is_hit)
        {
            if (0 == path_length)
            {
                result.hit_pos = primary_hit.position;
            }

            const float3 to_light_norm = SUN_DIRECTION;
            
            const bool is_shadowed = rt_is_shadowed(
                acceleration_structure,
                new_ray(
                    primary_hit.position,
                    to_light_norm,
                    1e-4,
                    FLT_MAX
            ));

            if (0 == path_length)
            {
                hit_dist_wt += float2(pack_dist(primary_hit.ray_t), 1);
            }

            GbufferData gbuffer = unpack(primary_hit.gbuffer_packed);
            

            const float3x3 tangent_to_world = build_orthonormal_basis(gbuffer.normal);
            const float3 wi = mul(to_light_norm, tangent_to_world);

            float3 wo = mul(-outgoing_ray.Direction, tangent_to_world);

            // Hack for shading normals facing away from the outgoing ray's direction:
            // We flip the outgoing ray along the shading normal, so that the reflection's curvature
            // continues, albeit at a lower rate.
            if (wo.z < 0.0)
            {
                wo.z *= -0.25;
                wo = normalize(wo);
            }

            LayeredBrdf brdf = LayeredBrdf::from_gbuffer_ndotv(gbuffer, wo.z);

            if (FIREFLY_SUPPRESSION)
            {
                brdf.specular_brdf.roughness = lerp(brdf.specular_brdf.roughness, 1.0, roughness_bias);
            }

            const float3 brdf_value = brdf.evaluate_directional_light(wo, wi);
            const float3 light_radiance = is_shadowed ? 0.0 : SUN_COLOR;
            irradiance_sum += throughput * brdf_value * light_radiance * max(0.0, wi.z);

            if (USE_EMISSIVE)
            {
                irradiance_sum += gbuffer.emissive * throughput;
            }

            
            
            if (SAMPLE_IRCACHE_AT_LAST_VERTEX && path_length + 1 == MAX_PATH_LENGTH)
            {
                irradiance_sum +=
                    IrcacheLookupParams::create(entry.position, primary_hit.position, gbuffer.normal)
                        .with_query_rank(1 + ircache_entry_life_to_rank(life))
                        .lookup(rng)
                        * throughput * gbuffer.albedo;
            }

            const float3 urand = float3(
                uint_to_u01_float(hash1_mut(rng)),
                uint_to_u01_float(hash1_mut(rng)),
                uint_to_u01_float(hash1_mut(rng))
            );

            BrdfSample brdf_sample = brdf.sample(wo, urand);

            // TODO: investigate NaNs here.
            if (brdf_sample.is_valid() && brdf_sample.value_over_pdf.x == brdf_sample.value_over_pdf.x)
            {
                roughness_bias = lerp(roughness_bias, 1.0, 0.5 * brdf_sample.approx_roughness);
                outgoing_ray.Origin = primary_hit.position;
                outgoing_ray.Direction = mul(tangent_to_world, brdf_sample.wi);
                outgoing_ray.TMin = 1e-4;
                throughput *= brdf_sample.value_over_pdf;
            }
            else
            {
                break;
            }
        }
        else
        {
            if (0 == path_length)
            {
                result.hit_pos = outgoing_ray.Origin + outgoing_ray.Direction * 1000;
            }

            if (far_field.is_hit())
            {
                irradiance_sum += throughput * far_field.radiance * far_field.inv_pdf;
            }
            else
            {
                if (0 == path_length)
                {
                    hit_dist_wt += float2(pack_dist(1), 1);
                }

                irradiance_sum += throughput * sample_environment_light(outgoing_ray.Direction);
            }

            break;
        }
    }

    result.incident_radiance = irradiance_sum;
    return result;
}
#endif
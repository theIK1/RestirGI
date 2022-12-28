#ifndef IRCACHE_SAMPLER_COMMON_INC_HLSL
#define IRCACHE_SAMPLER_COMMON_INC_HLSL

static const uint SAMPLER_SEQUENCE_LENGTH = 1024;

struct SampleParams {
    uint value; //像素位置,编码为 x|(y<<16)  比如(1,1)对应65537

    static SampleParams from_spf_entry_sample_frame(uint samples_per_frame, uint entry_idx, uint sample_idx, uint frame_idx) {
        const uint PERIOD = IRCACHE_OCTA_DIMS2 / samples_per_frame; //4

        uint xy = sample_idx * PERIOD + (frame_idx % PERIOD);// 0+1 4+1 8+1 12+1 

        // Checkerboard
        xy ^= (xy & 4u) >> 2u;

        SampleParams res;
        res.value = xy + ((frame_idx << 16u) ^ (entry_idx)) * IRCACHE_OCTA_DIMS2;

        return res;
    }

    static SampleParams from_raw(uint raw) {
        SampleParams res;
        res.value = raw;
        return res;
    }

    uint raw() {
        return value;
    }

    uint octa_idx() {
        return value % IRCACHE_OCTA_DIMS2;  //返回编号0-15
    }

    uint2 octa_quant() {
        uint oi = octa_idx(); 
        return uint2(oi % IRCACHE_OCTA_DIMS, oi / IRCACHE_OCTA_DIMS); //返回八面体纹理中的编号
    }

    uint rng() {
        return hash1(value >> 4u);
    }

    float2 octa_uv() {
        const uint2 oq = octa_quant();
        const uint r = rng();
        const float2 urand = r2_sequence(r % SAMPLER_SEQUENCE_LENGTH); //0-1随机值
        return (float2(oq) + urand) / 4.0;
    }

    // TODO: tackle distortion
    float3 direction() {
        return octa_decode(octa_uv());
    }
};

#endif  // IRCACHE_SAMPLER_COMMON_INC_HLSL

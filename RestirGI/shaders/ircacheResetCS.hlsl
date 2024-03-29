#include "frame_constants.hlsl"
#include "ircache_constants.hlsl"

StructuredBuffer<uint> ircache_life_buf : register(t0);
ByteAddressBuffer ircache_meta_buf : register(t1);
StructuredBuffer<float4> ircache_irradiance_buf : register(t2);
StructuredBuffer<uint> ircache_entry_indirection_buf : register(t3);

RWStructuredBuffer<float4> ircache_aux_buf : register(u0);






[numthreads(64, 1, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    
    uint dispatch_idx = DispatchThreadId.x;
    if (IRCACHE_FREEZE)
    {
        return;
    }

    const uint total_alloc_count = ircache_meta_buf.Load(IRCACHE_META_TRACING_ALLOC_COUNT);
    if (dispatch_idx >= total_alloc_count)
    {
        return;
    }

    const uint entry_idx = ircache_entry_indirection_buf[dispatch_idx];

    const bool should_reset = all(0.0 == ircache_irradiance_buf[entry_idx * IRCACHE_IRRADIANCE_STRIDE]);

    if (should_reset)
    {
        for (uint i = 0; i < IRCACHE_AUX_STRIDE; ++i)
        {
            ircache_aux_buf[entry_idx * IRCACHE_AUX_STRIDE + i] = 0.0.xxxx;
        }
    }

}
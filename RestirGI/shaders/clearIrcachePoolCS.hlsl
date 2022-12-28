#include "ircache_constants.hlsl"

RWStructuredBuffer<uint> ircache_pool_buf : register(u0);
RWStructuredBuffer<uint> ircache_life_buf : register(u1);

[numthreads(64, 1, 1)]
void CS(uint idx: SV_DispatchThreadID) {
    ircache_pool_buf[idx] = idx;
    ircache_life_buf[idx] = IRCACHE_ENTRY_LIFE_RECYCLED;
}

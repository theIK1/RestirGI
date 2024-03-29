#include "frame_constants.hlsl"
#include "ircache_constants.hlsl"

ByteAddressBuffer ircache_meta_buf : register(t0);


RWByteAddressBuffer dispatch_args : register(u0);





[numthreads(8, 8, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    
    // Aging ags
    {
        const uint entry_count = ircache_meta_buf.Load(IRCACHE_META_ENTRY_COUNT);

        static const uint threads_per_group = 64;
        static const uint entries_per_thread = 1;
        static const uint divisor = threads_per_group * entries_per_thread;

        dispatch_args.Store4(0 * sizeof(uint4), uint4((entry_count + divisor - 1) / divisor, 1, 1, 0));
    }

}
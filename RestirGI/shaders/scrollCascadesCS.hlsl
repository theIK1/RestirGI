#include "frame_constants.hlsl"
#include "hash.hlsl"

ByteAddressBuffer ircache_grid_meta_buf : register(t0);


RWStructuredBuffer<uint> ircache_entry_cell_buf : register(u0);
RWStructuredBuffer<float4> ircache_irradiance_buf : register(u1);
RWStructuredBuffer<uint> ircache_life_buf : register(u2);
RWStructuredBuffer<uint> ircache_pool_buf : register(u3);
RWByteAddressBuffer ircache_meta_buf : register(u4);
RWTexture2D<float4> RWTest : register(u5);

RWByteAddressBuffer ircache_grid_meta_buf2 : register(u6); 

#include "ircache_constants.hlsl"
#include "ircache_grid.hlsl" 

void deallocate_cell(uint cell_idx)
{
    const uint2 meta = ircache_grid_meta_buf.Load2(sizeof(uint2) * cell_idx);

    if (meta.y & IRCACHE_ENTRY_META_OCCUPIED)
    {
        // Clear the just-nuked entry
        const uint entry_idx = meta.x;

        ircache_life_buf[entry_idx] = IRCACHE_ENTRY_LIFE_RECYCLED;

        for (uint i = 0; i < IRCACHE_IRRADIANCE_STRIDE; ++i)
        {
            ircache_irradiance_buf[entry_idx * IRCACHE_IRRADIANCE_STRIDE + i] = 0.0.xxxx;
        }

        uint entry_alloc_count = 0;
  
        ircache_meta_buf.InterlockedAdd(IRCACHE_META_ALLOC_COUNT, -1, entry_alloc_count);
        ircache_pool_buf[entry_alloc_count - 1] = entry_idx;
    }
}


[numthreads(32, 1, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{

    
    const uint3 dst_vx = uint3(DispatchThreadId.xy, DispatchThreadId.z % IRCACHE_CASCADE_SIZE);
    const uint cascade = DispatchThreadId.z / IRCACHE_CASCADE_SIZE;

    const uint dst_cell_idx = IrcacheCoord::from_coord_cascade(dst_vx, cascade).cell_idx();

    const int3 scroll_by =
        IRCACHE_FREEZE
        ? (0).xxx
        : frame_constants.ircache_cascades[cascade].voxels_scrolled_this_frame.xyz;
    


    //!all(uint3(dst_vx - scroll_by) < IRCACHE_CASCADE_SIZE)
    if (!all(uint3(dst_vx - scroll_by) < IRCACHE_CASCADE_SIZE)) //!all(abs((int3) dst_vx - scroll_by) < IRCACHE_CASCADE_SIZE)
    {
        // If this entry is about to get overwritten, deallocate it.
        //deallocate_cell(dst_cell_idx);
        
    }

    const uint3 src_vx = dst_vx + scroll_by;

    if (all(src_vx < IRCACHE_CASCADE_SIZE))
    {
        const uint src_cell_idx = IrcacheCoord::from_coord_cascade(src_vx, cascade).cell_idx();

        const uint2 cell_meta = ircache_grid_meta_buf.Load2(sizeof(uint2) * src_cell_idx);
        ircache_grid_meta_buf2.Store2(sizeof(uint2) * dst_cell_idx, cell_meta);

        // Update the cell idx in the `ircache_entry_cell_buf`
        if (cell_meta.y & IRCACHE_ENTRY_META_OCCUPIED)
        {
            const uint entry_idx = cell_meta.x;
            ircache_entry_cell_buf[entry_idx] = dst_cell_idx;
        }
    }
    else
    {
        ircache_grid_meta_buf2.Store2(sizeof(uint2) * dst_cell_idx, (0).xx);
    }
   

}
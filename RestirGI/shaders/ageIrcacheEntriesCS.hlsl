#include "frame_constants.hlsl"
#include "hash.hlsl"
#include "mesh.hlsl"

RWByteAddressBuffer ircache_meta_buf : register(u0);
RWStructuredBuffer<uint> ircache_entry_cell_buf : register(u1);
RWStructuredBuffer<uint> ircache_life_buf : register(u2);
RWStructuredBuffer<uint> ircache_pool_buf : register(u3);
RWStructuredBuffer<VertexPacked> ircache_spatial_buf : register(u4);
RWStructuredBuffer<VertexPacked> ircache_reposition_proposal_buf : register(u5);
RWStructuredBuffer<uint> ircache_reposition_proposal_count_buf : register(u6);
RWStructuredBuffer<float4> ircache_irradiance_buf : register(u7);
RWStructuredBuffer<uint> entry_occupancy_buf : register(u8);

RWByteAddressBuffer ircache_grid_meta_buf : register(u9);

#include "ircache_constants.hlsl"

void age_ircache_entry(uint entry_idx)
{
    const uint prev_age = ircache_life_buf[entry_idx];
    const uint new_age = prev_age + 1; //新的age为在之前的age上加一，不断更新直到该age超过设定上限

    if (is_ircache_entry_life_valid(new_age)) //小于设定的排名等级即为合理
    {
        ircache_life_buf[entry_idx] = new_age; //更新相应entry的age

        // TODO: just `Store` it (AMD doesn't like it unless it's a byte address buffer)
        const uint cell_idx = ircache_entry_cell_buf[entry_idx]; //获取存储当前entry的cell index
        //更改该cell index的entry flag，如果为IRCACHE_ENTRY_META_JUST_ALLOCATED状态改为0状态，如果是IRCACHE_ENTRY_META_OCCUPIED则不变
        ircache_grid_meta_buf.InterlockedAnd(
            sizeof(uint2) * cell_idx + sizeof(uint),
            ~IRCACHE_ENTRY_META_JUST_ALLOCATED); 
    }
    else
    {
        ircache_life_buf[entry_idx] = IRCACHE_ENTRY_LIFE_RECYCLED; //age失效，该entry将被弃用
        // onoz, we killed it!
        // deallocate.

        for (uint i = 0; i < IRCACHE_IRRADIANCE_STRIDE; ++i) //清空该entry存储的Irradiance
        {
            ircache_irradiance_buf[entry_idx * IRCACHE_IRRADIANCE_STRIDE + i] = 0.0.xxxx;
        }

        uint entry_alloc_count = 0;
        ircache_meta_buf.InterlockedAdd(IRCACHE_META_ALLOC_COUNT, -1, entry_alloc_count); //当前分配entry数量减一
        ircache_pool_buf[entry_alloc_count - 1] = entry_idx; //entry池中索引为当前分配entry数量的位置内容为该entry index
        // entry_alloc_count = 10
        // 0 1 2 3 4 5 6 7 8 9  ----> 0
        // v i v i v v i i v i

        // TODO: just `Store` it (AMD doesn't like it unless it's a byte address buffer)
        const uint cell_idx = ircache_entry_cell_buf[entry_idx];
         //更改该cell index的entry flag为0状态
        ircache_grid_meta_buf.InterlockedAnd(
            sizeof(uint2) * cell_idx + sizeof(uint),
            ~(IRCACHE_ENTRY_META_OCCUPIED | IRCACHE_ENTRY_META_JUST_ALLOCATED));
    }
}

bool ircache_entry_life_needs_aging(uint life)
{
    return life != IRCACHE_ENTRY_LIFE_RECYCLED;
}


[numthreads(64, 1, 1)]
void CS(
    uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
    uint entry_idx = DispatchThreadId.x;
    const uint total_entry_count = ircache_meta_buf.Load(IRCACHE_META_ENTRY_COUNT);

    uint args = (total_entry_count + 63) / 64;
    if (entry_idx >= args * 64)
    {
        return;
    }
    
    if (!IRCACHE_FREEZE)
    {
        if (entry_idx < total_entry_count)
        {
            const uint life = ircache_life_buf[entry_idx];
            //从0到已经分配的entry数量依次检查，如果ircache_life_buf[entry_idx]的值被更改过，说明该entry需要更新
            if (ircache_entry_life_needs_aging(life)) 
            {
                age_ircache_entry(entry_idx);
            }

            #if IRCACHE_USE_POSITION_VOTING
                #if 0
                    uint rng = hash2(uint2(entry_idx, frame_constants.frame_index));
                    const float dart = uint_to_u01_float(hash1_mut(rng));
                    const float prob = 0.02;

                    if (dart <= prob)
                #endif
                {

                 // Flush the reposition proposal
                VertexPacked proposal = ircache_reposition_proposal_buf[entry_idx]; //更新体素的空间坐标为最新值
                ircache_spatial_buf[entry_idx] = proposal; 
            }
#endif

            ircache_reposition_proposal_count_buf[entry_idx] = 0; //每个entry_index的重投影数量变为0
        }
        else
        {
            VertexPacked invalid;
            invalid.data0 = asfloat(0);
            ircache_spatial_buf[entry_idx] = invalid; //对于未分配的entry_idx，信息更改为0
        }
    }

    const uint life = ircache_life_buf[entry_idx];
    uint valid = entry_idx < total_entry_count && is_ircache_entry_life_valid(life);
    entry_occupancy_buf[entry_idx] = valid; //只要该entry_index的life不合理或者超过现有分配entry数就设为0

}
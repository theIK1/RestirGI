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
    const uint new_age = prev_age + 1; //�µ�ageΪ��֮ǰ��age�ϼ�һ�����ϸ���ֱ����age�����趨����

    if (is_ircache_entry_life_valid(new_age)) //С���趨�������ȼ���Ϊ����
    {
        ircache_life_buf[entry_idx] = new_age; //������Ӧentry��age

        // TODO: just `Store` it (AMD doesn't like it unless it's a byte address buffer)
        const uint cell_idx = ircache_entry_cell_buf[entry_idx]; //��ȡ�洢��ǰentry��cell index
        //���ĸ�cell index��entry flag�����ΪIRCACHE_ENTRY_META_JUST_ALLOCATED״̬��Ϊ0״̬�������IRCACHE_ENTRY_META_OCCUPIED�򲻱�
        ircache_grid_meta_buf.InterlockedAnd(
            sizeof(uint2) * cell_idx + sizeof(uint),
            ~IRCACHE_ENTRY_META_JUST_ALLOCATED); 
    }
    else
    {
        ircache_life_buf[entry_idx] = IRCACHE_ENTRY_LIFE_RECYCLED; //ageʧЧ����entry��������
        // onoz, we killed it!
        // deallocate.

        for (uint i = 0; i < IRCACHE_IRRADIANCE_STRIDE; ++i) //��ո�entry�洢��Irradiance
        {
            ircache_irradiance_buf[entry_idx * IRCACHE_IRRADIANCE_STRIDE + i] = 0.0.xxxx;
        }

        uint entry_alloc_count = 0;
        ircache_meta_buf.InterlockedAdd(IRCACHE_META_ALLOC_COUNT, -1, entry_alloc_count); //��ǰ����entry������һ
        ircache_pool_buf[entry_alloc_count - 1] = entry_idx; //entry��������Ϊ��ǰ����entry������λ������Ϊ��entry index
        // entry_alloc_count = 10
        // 0 1 2 3 4 5 6 7 8 9  ----> 0
        // v i v i v v i i v i

        // TODO: just `Store` it (AMD doesn't like it unless it's a byte address buffer)
        const uint cell_idx = ircache_entry_cell_buf[entry_idx];
         //���ĸ�cell index��entry flagΪ0״̬
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
            //��0���Ѿ������entry�������μ�飬���ircache_life_buf[entry_idx]��ֵ�����Ĺ���˵����entry��Ҫ����
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
                VertexPacked proposal = ircache_reposition_proposal_buf[entry_idx]; //�������صĿռ�����Ϊ����ֵ
                ircache_spatial_buf[entry_idx] = proposal; 
            }
#endif

            ircache_reposition_proposal_count_buf[entry_idx] = 0; //ÿ��entry_index����ͶӰ������Ϊ0
        }
        else
        {
            VertexPacked invalid;
            invalid.data0 = asfloat(0);
            ircache_spatial_buf[entry_idx] = invalid; //����δ�����entry_idx����Ϣ����Ϊ0
        }
    }

    const uint life = ircache_life_buf[entry_idx];
    uint valid = entry_idx < total_entry_count && is_ircache_entry_life_valid(life);
    entry_occupancy_buf[entry_idx] = valid; //ֻҪ��entry_index��life��������߳������з���entry������Ϊ0

}
struct ShadowPayload
{
    bool is_shadowed;
};

[shader("closesthit")]
void ShadowRchit(inout ShadowPayload payload : SV_RayPayload)
{
    payload.is_shadowed = false;
}
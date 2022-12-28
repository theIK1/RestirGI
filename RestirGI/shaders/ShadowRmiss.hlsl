struct ShadowPayload
{
    bool is_shadowed;
};

[shader("miss")] 
void ShadowRmiss(inout ShadowPayload payload : SV_RayPayload) {
    payload.is_shadowed = false;
}
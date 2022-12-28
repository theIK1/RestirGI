#ifndef LUMEN_MATH_HLSL
#define LUMEN_MATH_HLSL

static const uint FixedJitterIndex = -1;
static const uint StateFrameIndexMod8 = 4;
static const uint NumRadianceProbeClipmaps = 4;
static const uint RadianceProbeClipmapResolution = 48;
static const uint RadianceProbeResolution = 32;
static const uint FinalRadianceAtlasMaxMip = 0;
static const uint MaxImportanceSamplingOctahedronResolution = 16;
static const uint ScreenProbeDownsampleFactor = 16;


static const uint2 ScreenProbeViewSize = uint2(120, 68);
static const uint NumUniformScreenProbes = 8160;

static const float SpatialFilterPositionWeightScale = 1000.0f;
static const float SpatialFilterMaxRadianceHitAngle = 0.17453f;
static const float PrevInvPreExposure = 0.35355f;
static const float PreExposure = 2.82843f;
static const float ImportanceSamplingHistoryDistanceThreshold = 30.00f;
static const float MinPDFToTrace = 0.1f;
static const float MaxHalfFloat = 65504.0f;
static const float MaxRayIntensity = 20.0f;

static const float4 ViewRectMin = float4(0.f, 0.f, 0.f, 0.f);
static const float4 ViewSizeAndInvSize = float4(1920.f, 1080.f, 0.00052, 0.00093); //1920.f,1080.f,1/1920.f,1/1080.f
static const float4 BufferSizeAndInvSize = float4(1922.f, 1083.f, 0.00052, 0.00092); //1922.f, 1083.f, 1 / 1922.f, 1 / 1083.f
static const float4 InvDeviceZToWorldZTransform = float4(0.0, 0.0, 0.1, -1E-08);
static const float4 ScreenPositionScaleBias = float4(0.49935, -0.49834, 0.49834, 0.49935);
static const float4 ImportanceSamplingHistoryScreenPositionScaleBias = float4(0.49935, -0.49834, 0.49834, 0.49935);
static const float4 ImportanceSamplingHistoryUVMinMax = float4(0.00033, 0.00055, 0.99836, 0.99613);

static const float WorldPositionToRadianceProbeCoordScale[6] = { 0.0096, 0.0048, 0.0024, 0.0012, -18766.1875, -0.0002 }; //后两位好像没用到
static const float RadianceProbeCoordToWorldPositionScale[6] = { 1 / 0.0096, 1 / 0.0048, 1 / 0.0024, 1 / 0.0012, 0., 0. }; //后两位暂时不用
static const float RadianceProbeClipmapTMin[6] = { 180.42197, 360.84393, 721.68787, 1443.37573, 0., 0. }; //后两位暂时不用
static const float3 WorldPositionToRadianceProbeCoordBias[6] =
{
    float3(-14.00, 925.00, -31.00), float3(5.00, 475.00, -3.00), float3(15.00, 250.00, 11.00), float3(20.00, 137.00, 18.00), float3(0., 0., 0.), float3(0., 0., 0.)
};

static const uint2 ScreenProbeAtlasViewSize = uint2(120, 102);
static const uint2 ScreenProbeAtlasBufferSize = uint2(120, 102);

static const uint MaxNumAdaptiveProbes = 4080;
//frame_constants.frame_index / 4
#define SCREEN_TEMPORAL_INDEX 0

#endif
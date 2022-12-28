#ifndef STRUCTURE_H
#define STRUCTURE_H
#include "ShaderInterop.h"

static const uint TraceNums = 8;




#define P1 73856093
#define P2 19349663
#define P3 83492791
#define TABLE_SIZE 261120 //120*68*8*8*5

static const float RTXGI_PI = 3.1415926535897932f;
static const float RTXGI_2PI = 6.2831853071795864f;

static const uint3 ProbeCount = uint3(120, 1, 68);//22 22 22

static const uint RayDataWidth = 120; //1920 / 16
static const uint RayDataHeight = 68; // 1080 / 16

static const uint DDGIRayDataWidth = 10648; // 22*22*22
static const uint DDGIRayDataHeight = 288; // 288

static const uint ScreenProbeTracingOctahedronResolution = TraceNums;  //8
static const uint ScreenProbeGatherOctahedronResolution = TraceNums; //8

static const uint IRCACHE_CASCADE_SIZE = 32;
static const uint IRCACHE_CASCADE_COUNT = 12;
static const uint MAX_ENTRIES = 1024 * 64;
static const uint INDIRECTION_BUF_ELEM_COUNT = 1024 * 1024;
static const uint MAX_GRID_CELLS = IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_COUNT;

static const float IRCACHE_GRID_CELL_DIAMETER = 0.16 * 0.125;

//enum class RenderOverrideFlags {
//	FORCE_FACE_NORMALS = 1u << 0,
//	NO_NORMAL_MAPS = 1u << 1,
//	FLIP_NORMAL_MAP_YZ = 1u << 2,
//	NO_METAL = 1u << 3
//};



struct SceneParams
{
	float3 lightPosition;
	float time;

	float4 probeRayRotation;

	int showRenderType;
	int showRenderMode;
	int enableAO;
	int patch1;

	float nearPlane;
	float farPlane;
	float lightPower;
	float offset;

};


struct AtrousConstantBuffer
{
	float stepWidth;
	float colorPhi;
	float normalPhi;
	float posPhi;

	float phiLuminance;
	float phiNormal;
	float phiDepth;
	float atrousType;
};


struct CameraConstantBuffer
{
	matrix view;
	matrix projection;
	matrix viewI;
	matrix projectionI;
	matrix lastview;
	matrix lastprojection;
	matrix lastviewI;
	matrix lastprojectionI;

	float fov;
	float aspect;
};

struct ViewConstants
{
	matrix view_to_clip;
	matrix clip_to_view;
	matrix view_to_sample;
	matrix sample_to_view;
	matrix world_to_view;
	matrix view_to_world;

	matrix clip_to_prev_clip;

	matrix prev_view_to_prev_clip;
	matrix prev_clip_to_prev_view;
	matrix prev_world_to_prev_view;
	matrix prev_view_to_prev_world;
	//taaÏà¹Ø
	float2 sample_offset_pixels;
	float2 sample_offset_clip;
};



struct RenderOverrides {
	uint flags;
	float material_roughness_scale;

	uint pad0;
	uint pad1;


};

struct IrcacheCascadeConstants {
	int4 origin;
	int4 voxels_scrolled_this_frame;
};

struct Size {
	uint UINT_SIZE;
	uint UINT2_SIZE;
	uint pad0;
	uint exposure;
};

struct FrameConstantBuffer
{
	ViewConstants view_constants;

	float4 sun_direction;

	uint frame_index;
	float delta_time_seconds;
	float sun_angular_radius_cos;
	uint triangle_light_count;

	float4 sun_color_multiplier;
	float4 sky_ambient;

	float pre_exposure;
	float pre_exposure_prev;
	float pre_exposure_delta;
	float pad0;

	RenderOverrides render_overrides;

	Size size;

	float4 ircache_grid_center;
	IrcacheCascadeConstants ircache_cascades[12];
};

struct ShadowSpatialCB
{
	float4 input_tex_size;
	uint2 bitpacked_shadow_mask_extent;
	uint step_size;
};

struct RestirSpatialCB
{
	float4 gbuffer_tex_size;
	float4 output_tex_size;
	uint spatial_reuse_pass_idx;
	// Only done in the last spatial resampling pass
	uint perform_occlusion_raymarch;
	uint occlusion_raymarch_importance_only;
};

struct ScreenProbeAdaptivePlacementCB
{
	uint PlacementDownsampleFactor;
};

struct FinalCB
{
	uint control;
};

struct IndirectCB
{
	uint showProbeRadiance;
	uint enableFilter;
	uint enableReservoir;
	float depthDiff;
	uint enableTemporal;
	
};

#endif
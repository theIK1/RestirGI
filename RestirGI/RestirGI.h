#pragma once
#include "core/DXSample.h"
#include <dxcapi.h>
#include <vector>
#include <map>
#include "core/D3DUtility.h"
#include "helper/TextureLoader.h"
#include "helper/TopLevelASGenerator.h"
#include "helper/ShaderBindingTableGenerator.h"
#include "helper/ImGuiImporter.h"
#include "helper/CS/ScreenProbeDownsampleDepthUniformCS.h"
#include "helper/RootSignatureGenerator.h"
#include <random>
#include "Structure.h"



using Microsoft::WRL::ComPtr;

class CS {
public:
	ComPtr<ID3D12RootSignature> RootSignature;
	ComPtr<ID3D12PipelineState> PiplineState;
	std::vector<std::string> srvResource;
	std::vector<std::string> uavResource;
private:
	std::vector<uint> srvIndex;
	std::vector<uint> uavIndex;
	uint cbvNums;
	uint readNums; //表示有几组不固定的读取纹理
	uint writeNums; //表示有几组不固定的写入纹理
	uint bindlessOffset; //表示bindless的偏移量。0表示不使用

	std::wstring path;
	std::wstring entryPoint;

	ComPtr<ID3D12Device5>				m_device;
	LPCWSTR* macro = nullptr;

	//uint useTexture; //0代表使用只uav，1代表只使用srv，2代表都使用
	bool useSamples;

	std::array<CD3DX12_STATIC_SAMPLER_DESC, 6> staticSamplers;
public:
	CS(ComPtr<ID3D12Device5> device,
		const std::wstring CSpath,
		const std::wstring entry,
		std::vector<uint>& srv,
		std::vector<uint>& uav,
		uint cbv,
		uint2 readORwrite = uint2(0, 0),
		bool Samples = false,
		std::array<CD3DX12_STATIC_SAMPLER_DESC, 6> samples = std::array<CD3DX12_STATIC_SAMPLER_DESC, 6>{},
		uint bindless = 0,
		LPCWSTR* Marco = nullptr
		) {

		m_device = device;
		path = CSpath;
		entryPoint = entry;
		srvIndex.swap(srv);
		uavIndex.swap(uav);
		cbvNums = cbv;
		readNums = readORwrite.x;
		writeNums = readORwrite.y;
		bindlessOffset = bindless;
		useSamples = Samples;
		staticSamplers.swap(samples);
		macro = Marco;
		createSignature();
		createPipeLine();

		auto begin = CSpath.find(L"/") + 1;
		auto end = CSpath.find(L".");
		std::wstring name = CSpath.substr(begin, end - begin);
		RootSignature->SetName((name + L"RSG").c_str());
		PiplineState->SetName((name + L"PSO").c_str());
		
	}

private:
	void createSignature() {
		nv_helpers_dx12::RootSignatureGenerator RSG;

		std::vector<std::tuple<UINT, /* BaseShaderRegister, */ UINT, /* NumDescriptors */ UINT,
			/* RegisterSpace */ D3D12_DESCRIPTOR_RANGE_TYPE,
			/* RangeType */ UINT /* OffsetInDescriptorsFromTableStart */>>
			ranges;
		//srv
		if (srvIndex.size() != 0)
		{
			for (uint i = 0; i < srvIndex.size(); ++i)
			{
				ranges.push_back({ i,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV ,srvIndex[i] });
			}
			RSG.AddHeapRangesParameter(ranges);
			ranges.clear();
		}

		//uav
		if (uavIndex.size() != 0)
		{
			for (uint i = 0; i < uavIndex.size(); ++i)
			{
				ranges.push_back({ i,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV ,uavIndex[i] });
			}
			RSG.AddHeapRangesParameter(ranges);
			ranges.clear();
		}

		for (uint i = 0; i < readNums; ++i)
		{
			RSG.AddHeapRangesParameter({ { srvIndex.size() + i,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV ,0 }});
		}

		for (uint i = 0; i < writeNums; ++i)
		{
			RSG.AddHeapRangesParameter({ { uavIndex.size() + i,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV ,0 } });
		}

		for (uint i = 0; i < cbvNums; ++i)
		{
			RSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, i);
		}

		if(bindlessOffset > 0)
			RSG.AddHeapRangesParameter({ { 1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV ,bindlessOffset } });

		if (!useSamples)
			RootSignature = RSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		else
			RootSignature = RSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, (UINT)staticSamplers.size(), staticSamplers.data());
	}

	void createPipeLine() {
		ComPtr<IDxcBlob> CS = helper::CompileShader(path, macro, entryPoint, L"cs_6_0");

		D3D12_COMPUTE_PIPELINE_STATE_DESC PSO = {};
		PSO.pRootSignature = RootSignature.Get();
		PSO.CS = CD3DX12_SHADER_BYTECODE(CS->GetBufferPointer(),CS->GetBufferSize());
		PSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		ThrowIfFailed(m_device->CreateComputePipelineState(&PSO, IID_PPV_ARGS(&PiplineState)));
	}
};

class RestirGI : public DXSample
{
public:
	RestirGI(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

	virtual void OnButtonDown(UINT32 lParam);
	virtual void OnMouseMove(UINT8 wParam, UINT32 lParam);
	virtual void OnKeyDown(UINT8);
	virtual void OnKeyUp(UINT8);

private:
	//common base
	static const UINT					FrameCount = 2;
	static const UINT					RasterGBufferCount = 4;

	static const UINT					MRTCount = 200; //all pass render target nums

	CD3DX12_VIEWPORT					m_viewport;
	CD3DX12_RECT						m_scissorRect;
	ComPtr<IDXGISwapChain3>				m_swapChain;
	ComPtr<ID3D12Device5>				m_device;
	ComPtr<ID3D12Resource>				m_renderTargets[FrameCount];

	ComPtr<ID3D12CommandAllocator>		m_commandAllocator;
	ComPtr<ID3D12CommandQueue>			m_commandQueue;
	ComPtr<ID3D12GraphicsCommandList4>	m_commandList;

	//----------------------------------------
	//0-3: for RasterGbufferPass.hlsl , gbuffer/normal/world position/move vector
	//4	 : for TemporalFilterPixel.hlsl, temporal texture
	//5  : TBD
	//6  : TBD
	//7-8: for PostProcessingPixel.hlsl, atrous texture0 and 1
	//15 - history buffer,dont have rtv,only have srv
	//     --15: history color buffer
	//20 - later CS
	//		--20:OctacheralSolidAngleTexture

	ComPtr<ID3D12Resource>				m_rtBuffer[MRTCount];
	ComPtr<ID3D12Resource>				m_dsBuffer;

	ComPtr<ID3D12Resource> m_defaultTexture;
	ComPtr<ID3D12Resource> m_uploadTexture;

	DXGI_FORMAT m_dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_rtvFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

	DXGI_FORMAT getRTVFormat(std::string rtv);



	//--------------------------------------
	//size: FrameCount + MRTCount
	//0-1: render target for swap chain
	//2-5: render target for RasterGbufferPass.hlsl
	//    --2: gbuffer
	//    --3: normal
	//    --4: world position
	//    --5: move vector
	//6: render target for temporal filter
	//7: TBD
	//8: TBD
	//9-10: render target for atrous filter
	ComPtr<ID3D12DescriptorHeap>		m_rtvHeap;
	//--------------------------------------
	// size: 1
	ComPtr<ID3D12DescriptorHeap>		m_dsvHeap; 

	//----------------------------------------------
	//raster gbuffer format
	//            R32      G32      B32      A32
	//gbuffer		diffuseTex.rgb*a     viewSpacePosition.z
	//normal			input.normal         meshID
	//position         worldPosition      derivativeDepth
	//movevector 
	//--------------------------------------
	//size:5000
	//0:raytracing output resource (uav)
	//1:raytracing acceleration structure(srv)
	//2:scene(ex.light)(cbv)

	//10 -:resource for ray tracing pass:
	//     --10-13:albedo/normal/world position/move vector(rtv -> srv)
	//     --14   :depth(dsv->srv)
	//     --19  : MeshMaterialBuffer(srv)
	//20 -:resource for Temporal Filter pass: 
	//     --20: raytracing output resource(uav->srv)
	//     --25: default texture


	//50 -:resource for SVGF ATrous Filter pass:
	//	   --50:color ,is temporal filter pass output - temporal texture (rtv->srv)
	//     --11:raster normal(used)
	//     --12:raster world position(used)
	//     --14:raster depth(used)
	//60 -:resource for post processing pass
	//     --60-61:atrous filter pass output, atrous texture(0 or 1)(rtv->srv)
	// 70 -: resource for CS
#define MESH_TEXTURE_STARTINDEX 400
	//300 -:mesh texture(srv)
	//399  LDR_RGBA_0.png
	ComPtr<ID3D12DescriptorHeap>		m_rtSrvUavHeap;

	struct HandleOffset
	{
		uint resourceBuffer;
		uint srvHandle;
		uint uavHandle;
		HandleOffset(uint rt = 0, uint srv = 0, uint uav = 0) :resourceBuffer(rt), srvHandle(srv), uavHandle(uav) {}
	};

	enum class ResourceState
	{
		COMMON,
		READ,
		WRITE
	};

	std::map<std::string, HandleOffset> TexMap;
	std::map<std::string, ResourceState> TexState;//管理状态
	uint resourceBufferOffset = 11; //m_rtBuffer开始偏移的位置
	uint srvAnduavOffset = 70; //m_rtSrvUavHeap开始偏移的位置
	uint frametime = 0;//用以表示上一帧和下一帧

	std::vector<uint> getsrvHandle(std::vector<std::string> Arr)
	{
		std::vector<uint> ans;
		for (const auto& temp : Arr)
		{	
			if (TexMap.find(temp) == TexMap.end())
				break;
			ans.push_back(TexMap[temp].srvHandle);
		}
			
		return ans;
	}
	std::vector<uint> getuavHandle(std::vector<std::string> Arr)
	{
		std::vector<uint> ans;
		for (const auto& temp : Arr)
		{
			if (TexMap.find(temp) == TexMap.end())
				break;
			ans.push_back(TexMap[temp].uavHandle);
		}
			
		return ans;
	}



	UINT m_rtvDescriptorSize;
	UINT m_dsvDescriptorSize;
	UINT m_cbvSrvUavDescriptorSize;

	void Initialize();
	void LoadAssets();
	void WaitForPreviousFrame();

	std::unordered_map<std::string, std::unique_ptr<Mesh>> m_meshes;
	Model m_sceneModel;

	//raster Pipeline objects.
	ComPtr<ID3D12RootSignature> m_rasterRootSignature;
	ComPtr<ID3D12RootSignature> m_rasterTemporalFilterSignature;
	ComPtr<ID3D12RootSignature> m_rasterAtrousFilterSignature;
	ComPtr<ID3D12RootSignature> m_rasterPostProcessingSignature;

	

	ComPtr<ID3D12PipelineState> m_rasterPiplineState;
	ComPtr<ID3D12PipelineState> m_rasterGBufferPSO;//gbuffer first pass
	ComPtr<ID3D12PipelineState> m_rasterLightPSO;//
	ComPtr<ID3D12PipelineState> m_rasterTemporalFilterPSO;//
	ComPtr<ID3D12PipelineState> m_rasterAtrousFilterPSO;
	ComPtr<ID3D12PipelineState> m_rasterPostProcessingPSO;


	//Lumen
	std::shared_ptr<CS> indirectCS;
	std::shared_ptr<CS> screenProbeDownSampleDepthCS;
	std::shared_ptr<CS> clearProbeCS;
	std::shared_ptr<CS> screenProbeAdaptivePlacementCS;
	std::shared_ptr<CS> screenProbeConvertToSHCS;
	std::shared_ptr<CS> screenProbeFilterCS;

	std::shared_ptr<CS> LumenReprojectCS;
	std::shared_ptr<CS> temporalCS;
	std::shared_ptr<CS> spatialCS;
	std::shared_ptr<CS> lightCS;
	

	//restirGI
	std::shared_ptr<CS> brdfFgCS;
	std::shared_ptr<CS> skyCubeCS;
	std::shared_ptr<CS> convolveSkyCS;
	
	std::shared_ptr<CS> reprojectionMapCS;
	std::shared_ptr<CS> copyDepthCS;
	std::shared_ptr<CS> extractViewNormalCS;
	std::shared_ptr<CS> extractHalfDepthCS;

	std::shared_ptr<CS> ssaoCS;
	std::shared_ptr<CS> ssaoSpatialCS;
	std::shared_ptr<CS> ssaoUnsampleCS;
	std::shared_ptr<CS> ssaoTemporalCS;

	std::shared_ptr<CS> clearIrcachePoolCS;

	std::shared_ptr<CS> scrollCascadesCS;
	std::shared_ptr<CS> _ircacheDispatchArgsCS;
	std::shared_ptr<CS> ageIrcacheEntriesCS;

	std::shared_ptr<CS> _prefixScan1CS;
	std::shared_ptr<CS> _prefixScan2CS;
	std::shared_ptr<CS> _prefixScanMergeCS;

	std::shared_ptr<CS> ircacheCompactCS;
	std::shared_ptr<CS> _ircacheDispatchArgsCS2;
	std::shared_ptr<CS> ircacheResetCS;

	std::shared_ptr<CS> rtdgiReprojectCS;

	std::shared_ptr<CS> shadowBitpackCS;
	std::shared_ptr<CS> shadowTemporalCS;
	std::shared_ptr<CS> shadowSpatialCS;

	std::shared_ptr<CS> ircacheSumCS;
	std::shared_ptr<CS> extractHalfSSaoCS;

	std::shared_ptr<CS> validityIntegrateCS;
	std::shared_ptr<CS> restirTemporalCS;
	std::shared_ptr<CS> restirSpatialCS;
	std::shared_ptr<CS> restirResolveCS;
	std::shared_ptr<CS> rtdgiTemporalCS;
	std::shared_ptr<CS> rtdgiSpatialCS;

	std::shared_ptr<CS> lightGbufferCS;
	std::shared_ptr<CS> postCombineCS;
	std::shared_ptr<CS> finalCS;

	std::shared_ptr<CS> clearPathTraceCS;

	//Debug
	std::shared_ptr<CS> copyTestCS;
	std::shared_ptr<CS> calRMSECS;
	std::shared_ptr<CS> calRMSENumCS;


	void CreateGBufferResource();
	void CreateCommonRasterPipeline();
	ComPtr<ID3D12Resource>		m_rasterObjectCB;

	// Synchronization objects.
	UINT				m_frameIndex;
	HANDLE				m_fenceEvent;
	ComPtr<ID3D12Fence>	m_fence;
	UINT64				m_fenceValue;

	//Create and update all Constant Buffer
	void CreateAllConstantBuffer();
	void UpdateAllConstantBuffer();

	// Perspective Camera

	//std::unique_ptr<UploadBuffer<CameraConstantBuffer>>	m_cameraBuffer;
	float m_cameraMoveStep = 1.0;
	bool m_keys[256]{};
	//void CreateCameraBuffer();
	//void UpdateCameraBuffer();



	//Scene
	std::unique_ptr<UploadBuffer<SceneParams>> m_sceneBuffer;
	void CreateSceneBuffer();
	void UpdateSceneBuffer();

	//TemporalFilter
	struct TemporalConstantBuffer
	{
		int bTemporal;
	};
	std::unique_ptr<UploadBuffer<TemporalConstantBuffer>> m_temporalBuffer;
	void CreateTemporalBuffer();
	void UpdateTemporalBuffer();

	//AtrousFilter

	std::unique_ptr<UploadBuffer<AtrousConstantBuffer>>	m_atrousBuffer;
	void CreateAtrousBuffer();
	//void UpdateAtrousBuffer(UINT i);


	//Post Processing
	struct PostProcessingConstantBuffer
	{
		int showRenderType;
	};
	std::unique_ptr<UploadBuffer<PostProcessingConstantBuffer>>	m_postProcessingBuffer;
	void CreatePostProcessingBuffer();
	void UpdatePostProcessingBuffer();

	//Frame Constant
	std::unique_ptr<UploadBuffer<FrameConstantBuffer>>	m_frameBuffer;
	void CreateFrameBuffer();
	void UpdateFrameBuffer();

	//CS cbuffer
	std::unique_ptr<UploadBuffer<ShadowSpatialCB>>	m_shadowSpatialCB1;
	std::unique_ptr<UploadBuffer<ShadowSpatialCB>>	m_shadowSpatialCB2;
	std::unique_ptr<UploadBuffer<ShadowSpatialCB>>	m_shadowSpatialCB3;
	void CreateShadowSpatialCB();

	std::unique_ptr<UploadBuffer<RestirSpatialCB>> m_restirSpatialCB1;
	std::unique_ptr<UploadBuffer<RestirSpatialCB>> m_restirSpatialCB2;
	void CreateRestirSpatialCB();

	std::unique_ptr<UploadBuffer<FinalCB>> m_finalCB;
	void CreateFinalCB();
	void UpdateFinalCB();

	std::unique_ptr<UploadBuffer<ScreenProbeAdaptivePlacementCB>>	m_screenProbeAdaptivePlacementCB1;
	std::unique_ptr<UploadBuffer<ScreenProbeAdaptivePlacementCB>>	m_screenProbeAdaptivePlacementCB2;
	void CreateScreenProbeAdaptivePlacementCB();

	std::unique_ptr<UploadBuffer<IndirectCB>> m_indirectCB;
	void CreateIndirectCB();
	void UpdateIndirectCB();
	

	//texture 
	TextureLoader m_textloader;
	TextureLoader m_ddsloader; //加载dds图片

	//GUI
	ImGuiImporter* m_imGuiImporter;

	//Ray Tracing
	bool m_raster = false;
	void CheckRaytracingSupport();

	//DXR Acceleration Structure objects
	struct AccelerationStructureBuffers {
		ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
		ComPtr<ID3D12Resource> pResult;       // Where the AS is
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};

	ComPtr<ID3D12Resource>					m_bottomLevelAS; 
	nv_helpers_dx12::TopLevelASGenerator	m_topLevelASGenerator;
	AccelerationStructureBuffers			m_topLevelASBuffers;
	std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

	AccelerationStructureBuffers CreateBottomLevelAS(
		std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
		std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers = {});
	void CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances);
	void CreateAccelerationStructures();

	struct RayTracingShaderLibrary {
		std::string					name; //user define
		ComPtr<IDxcBlob>			library;
		std::vector<std::wstring>	exportSymbols;
		ComPtr<ID3D12RootSignature> signature;   //local signature for each raytracing shader
	};

	//dxr signature
	ComPtr<ID3D12RootSignature> m_rtGlobalSignature;

	std::vector<RayTracingShaderLibrary> m_rtShaderLibrary;
	RayTracingShaderLibrary CreateRayTracingShaderLibrary(std::string name, LPCWSTR shadername,
		std::vector<std::wstring> exportSymbols, ComPtr<ID3D12RootSignature> signature);


	// Ray tracing pipeline state
	ComPtr<ID3D12StateObject>			m_rtStateObject;
	ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;
	void CreateRaytracingPipeline();



	struct RT
	{
		ComPtr<ID3D12RootSignature>                  GlobalSignature;
		ComPtr<ID3D12StateObject>                    StateObject;
		ComPtr<ID3D12StateObjectProperties>          StateObjectProps;
		nv_helpers_dx12::ShaderBindingTableGenerator SBTHelper;
		ComPtr<ID3D12Resource>					     SBTStorage;
		D3D12_DISPATCH_RAYS_DESC					 desc = {};
		std::vector<std::string>                          srv;
		std::vector<std::string>                          uav;
		

		void bindDesc(ComPtr<ID3D12GraphicsCommandList4>m_commandList, uint width, uint height, uint depth)
		{
			m_commandList->SetPipelineState1(StateObject.Get());
			m_commandList->SetComputeRootSignature(GlobalSignature.Get());
			
			// The ray generation shaders are always at the beginning of the SBT.
			uint32_t rayGenerationSectionSizeInBytes = SBTHelper.GetRayGenSectionSize();
			desc.RayGenerationShaderRecord.StartAddress = SBTStorage->GetGPUVirtualAddress();
			desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;
			// The miss shaders are in the second SBT section
			uint32_t missSectionSizeInBytes = SBTHelper.GetMissSectionSize();
			desc.MissShaderTable.StartAddress = SBTStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
			desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
			desc.MissShaderTable.StrideInBytes = SBTHelper.GetMissEntrySize();



			// The hit groups section start after the miss shaders.
			uint32_t hitGroupsSectionSize = SBTHelper.GetHitGroupSectionSize();
			desc.HitGroupTable.StartAddress = SBTStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes + missSectionSizeInBytes;
			desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
			desc.HitGroupTable.StrideInBytes = SBTHelper.GetHitGroupEntrySize();

			// Dimensions of the image to render, identical to a kernel launch dimension

			desc.Width = width;
			desc.Height = height;


			desc.Depth = depth;
		}

		void generate(ComPtr<ID3D12Device5>	m_device)
		{
			uint32_t sbtSize = SBTHelper.ComputeSBTSize();
			SBTStorage = helper::CreateBuffer(m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
				D3D12_RESOURCE_STATE_GENERIC_READ, helper::kUploadHeapProps);

			if (!SBTStorage) {
				throw std::logic_error("Could not allocate the shader binding table");
			}

			SBTHelper.Generate(SBTStorage.Get(), StateObjectProps.Get());
		}
	};
	
	std::shared_ptr<RT> ircacheTraceAccessRT;//ircache trace access
	std::shared_ptr<RT> ircacheVaildateRT;//ircache validate
	std::shared_ptr<RT> ircacheTraceRT; // ircache trace
	std::shared_ptr<RT> traceShadowMaskRT; //trace shadow mask
	std::shared_ptr<RT> rtdgiValidateRT; //rtdgi validate
	std::shared_ptr<RT> rtdgiTraceRT; //rtdgi trace
	std::shared_ptr<RT> reflectionTraceRT; //reflection trace

	std::shared_ptr<RT> pathTraceRT; //path tracing

	std::shared_ptr<RT> LumenRT; //LumenGI
	std::shared_ptr<RT> LumenVaildateRT; //


	//SBT
	ComPtr<ID3D12Resource>							m_outputResource;
	nv_helpers_dx12::ShaderBindingTableGenerator	m_sbtHelper;
	ComPtr<ID3D12Resource>							m_sbtStorage;
	void CreateRayTracingResource();
	void CreateShaderBindingTable();

	

	/// Per-instance properties
	struct InstanceProperties {
		XMMATRIX objectToWorld;
		XMMATRIX objectToWorldNormal;
	};
	std::unique_ptr<UploadBuffer<InstanceProperties>> m_instanceProperties; // world inverse matrix for raytracing
	void CreateInstancePropertiesBuffer();
	void UpdateInstancePropertiesBuffer();

	matrix getReverseZ(float FovAngleY,
		float AspectRatio,
		float NearZ,
		float FarZ);

	//Returns a vector containing the largest integer less than or equal to a number for each element of self
	uint3 floor(float3 vec)
	{
		uint x = vec.x;
		uint y = vec.y;
		uint z = vec.z;
		uint m = min(min(x, y), z);
		return uint3(m, m, m);
	}


private:
	//scene setting
	bool m_enableAO = false;
	bool m_enableTAA = false;

	enum ShowRenderMode{
		COMMON = 0,
		DEFERRED_RENDERING
	};
	int m_showRenderMode = ShowRenderMode::DEFERRED_RENDERING;
	

	enum ShowRenderType {
		PATH_TRACTING = 0,
		DIRECT_LIGHT =1,	
		ALBEDO = 2,
		DEPTH = 3,
		NORMAL = 4,
		AO = 5,
		POSITION =6,
		RESTIR = 7,
		DIFFUSE = 8,
		IRCACHE = 9,
		LUMEN = 10,
	};

	matrix m_lastView = XMMatrixIdentity(); //如果view矩阵发送了变化，那么记录上一帧的变化，否则维持不变。
	matrix m_lastProjection = XMMatrixIdentity();

	matrix worldTransform = DirectX::XMMatrixScaling(0.1, 0.1, 0.1);

	uint frameTime = 0; //表示已经过了多少帧
	bool initialized = false; //表示是否为第一次执行

	float3 m_lightPos{ 0,1,0 };
	
	int  m_showRenderType = ShowRenderType::RESTIR;
	int  m_lastRenderType = ShowRenderType::RESTIR;;

	int m_atrousFilterType = 1;
	int m_atrousFilterNum = 0;

	float m_phiLuminance = 4;
	float m_phiNormal = 128;
	float m_phiDepth = 1.0;

	bool m_bTemporal = false;
	
	bool m_filter = false; //测试空间过滤
	bool m_enableDirectLight = false; //打开直接光照

	bool isMouseMove = false;
	bool isKeyboardMove = false;

	float3 DDGIorigin = float3(-0.4, 5.4, -0.25);

	float nearPlane = 0.01f;
	float farPlane = 10000.0f;
	float lightPower = 1.0f;
	float offset = 1.0f;
	float expousre = 1.0f;

	float depth_thresold = 0.01f;

	std::vector<float2> supersample_offsets;


	float4 m_probeRayRotationQuaternion = { 0.f, 0.f, 0.f, 1.f };

	//View constant
	float4 RectMin = { 0.f,0.f,0.f,0.f };
	float4 SizeAndInvSize = { 1920.f,1080.f,1/1920.f,1/1080.f };
	float4 BSizeAndInvSize = { 1922.f,1083.f,1 / 1922.f,1 / 1083.f };
	uint2 AtlasViewSize = { 120,102 }; //120,68 + 68/2

	//Probe
	uint2 probeTexSize;

	//Control
	
	
	bool importsampleRayGen = false;
	
	float m_threshold = 0.1f;
	bool USE_SCREEN_GI_REPROJECTION = true; //第一位
	bool USE_IRCACHE = true; //第二位
	bool USE_EXPOSURE = false; //第三位

	bool USE_shotcut = true; //第四位
	bool USE_TONE_MAPPING = true; //第五位

	enum class ShowDebug
	{
		RTDGI_VALIDATE,
		RTDGI_TRACE,
		VALIDITY_INTEGRATE,
		RESTIR_TEMPORAL,
		RESTIR_SPATIAL,
		RESTIR_RESOLVE,
		RTDGI_TEMPORAL,
		RTDGI_SPATIAL,
		FINAL,
		TEST,
		DEFAULT,
	};
	ShowDebug m_finalDebug = ShowDebug::DEFAULT;
	int typeLocked = -1; //确定是谁锁定
	bool isHoverd = false;
	

	bool m_probeIrradianceFormat = false; //SH

	bool ShowProbeRadiance = false; //显示探针辐射度纹理
	bool EnableFilter = false;
	bool EnableReservoir = true;
	bool EnableVaildateRay = true;
	bool Enabletemporal = true;

	//Frame
	int3 prev_scroll[IRCACHE_CASCADE_COUNT]{};
	int3 cur_scroll[IRCACHE_CASCADE_COUNT]{};


	// Compute the average frames per second and million rays per second.
	std::wstring m_info;
	void CalculateFrameStats();
	std::array<CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	//get gpu descriptor handle from m_rtSrvUavHeap
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT offset);

	LPCWSTR stringToLPCWSTR(std::string orig)
	{
		size_t origsize = orig.length() + 1;
		const size_t newsize = 100;
		size_t convertedChars = 0;
		wchar_t* wcstring = (wchar_t*)malloc(sizeof(wchar_t) * (orig.length() - 1));
		mbstowcs_s(&convertedChars, wcstring, origsize, orig.c_str(), _TRUNCATE);

		return wcstring;
	}

	void swapHandle(std::string A, std::string B)
	{
		std::swap(TexMap[A].srvHandle, TexMap[B].srvHandle);
		std::swap(TexMap[A].uavHandle, TexMap[B].uavHandle);
		std::swap(TexMap[A].resourceBuffer, TexMap[B].resourceBuffer);
		std::swap(TexState[A], TexState[B]);
	}
};


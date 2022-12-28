#pragma once
#include "helper/CS/createCS.h"

class SPDownSample 
{
public:
	SPDownSample(ID3D12Device5* device,
		UINT width, UINT height,
		DXGI_FORMAT format);
	//void BuildCSResources() ;
	void BuildCSDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
		UINT descriptorSize);
	//void BuildCSRootSignature() ;
	void Execute(ID3D12GraphicsCommandList* cmdList,
		ID3D12RootSignature* rootSig,
		ID3D12PipelineState* PSO,
		ID3D12Resource* input,
		ID3D12DescriptorHeap* rtSrvUavHeap
	);


private:
	void BuildDescriptors();
	void BuildResources();

private:

	ID3D12Device5* md3dDevice = nullptr;

	UINT mWidth = 0;
	UINT mHeight = 0;
	DXGI_FORMAT mFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT DepthFormat = DXGI_FORMAT_R32_UINT;
	DXGI_FORMAT SpeedFormat = DXGI_FORMAT_R16_FLOAT;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mSceneDepthCpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mSceneDepthCpuUav;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mWorldSpeedCpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mWorldSpeedCpuUav;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mSceneDepthGpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mSceneDepthGpuUav;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mWorldSpeedGpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mWorldSpeedGpuUav;

	ComPtr<ID3D12Resource> ScreenProbeSceneDepth = nullptr;
	ComPtr<ID3D12Resource> ScreenProbeWorldSpeed = nullptr;

	//ComPtr<ID3D12DescriptorHeap>		m_rtSrvUavHeap;

};
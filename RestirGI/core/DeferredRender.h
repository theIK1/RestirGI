#pragma once

using Microsoft::WRL::ComPtr;

class DeferredRender
{
public:
	DeferredRender(ID3D12Device5* device,UINT width,UINT height);

	void Init();

private:
	void CreateRTV();
	void CreateDSV();

	void CreateRootSignature();
	void CreateGBufferPSO();
	void CreateLightPSO();

	ComPtr<ID3D12RootSignature> m_rootSignature;
	//first pass: generate G-Buffer
	ComPtr<ID3D12PipelineState> m_gbufferPSO;
	//second pass : apply light
	ComPtr<ID3D12PipelineState> m_lightPSO;

	//-----------------------------------
	//0: CBV-Camera data
	//1: CBV-Light data
	//2: SRV-Normal(RTV->SRV)
	//3: SRV-Diffuse Albedo(RTV->SRV)
	//4: SRV-Depth(DSV->SRV)
	ComPtr<ID3D12DescriptorHeap> m_cbvsrvHeap;
	//
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

	//Render Target Buffer
	const static int RT_BUFFER_NUM = 2;
	ComPtr<ID3D12Resource> m_rtBuffer[RT_BUFFER_NUM];
	//Depth and  stencil Buffer
	ComPtr<ID3D12Resource> m_dsBuffer;

	DXGI_FORMAT m_dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_rtvFormat[2] = {
		DXGI_FORMAT_R8G8B8A8_SNORM,	 //normal
		DXGI_FORMAT_R11G11B10_FLOAT //diffuse albedo
		//DXGI_FORMAT_R8G8B8A8_UNORM   //
	};


	//
	ID3D12Device5* m_device;
	UINT m_width;
	UINT m_height;

};


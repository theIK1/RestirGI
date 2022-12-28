#include "stdafx.h"
#include "DeferredRender.h"
#include "helper/DXSampleHelper.h"


DeferredRender::DeferredRender(ID3D12Device5* device,UINT width, UINT height):
	m_device(device),m_width(width),m_height(height)
{
}

void DeferredRender::Init()
{
}

void DeferredRender::CreateRTV()
{
	m_rtvHeap = helper::CreateDescriptorHeap(m_device, RT_BUFFER_NUM, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);

	//create buffer
	CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Alignment = 0;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Width = m_width;
	resourceDesc.Height = m_height;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	float mClearColor[4] = { 0.0,0.0f,0.0f,1.0f };
	for (int i = 0; i < RT_BUFFER_NUM; i++) {
		CD3DX12_CLEAR_VALUE clearValue(m_rtvFormat[i], mClearColor);
		resourceDesc.Format = m_rtvFormat[i];
		ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(m_rtBuffer[i].GetAddressOf())));
	}
	//create rtv
	D3D12_RENDER_TARGET_VIEW_DESC desc;
	desc.Texture2D.MipSlice = 0;
	desc.Texture2D.PlaneSlice = 0;
	desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (int i = 0; i < RT_BUFFER_NUM; i++) {
		desc.Format = m_rtvFormat[i];
		m_device->CreateRenderTargetView(m_rtBuffer[i].Get(), &desc, handle);
		handle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	//create rts' srv
	D3D12_SHADER_RESOURCE_VIEW_DESC descSRV;
	descSRV.Texture2D.MipLevels = resourceDesc.MipLevels;
	descSRV.Texture2D.MostDetailedMip = 0;
	descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvsrvHandle(m_cbvsrvHeap->GetCPUDescriptorHandleForHeapStart());
	UINT desSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	cbvsrvHandle.Offset(2, desSize);//in cbvsrv heap,from index 2
	for (int i = 0; i < RT_BUFFER_NUM; i++) {
		descSRV.Format = m_rtvFormat[i];
		m_device->CreateShaderResourceView(m_rtBuffer[i].Get(), &descSRV, cbvsrvHandle);
		cbvsrvHandle.Offset(1, desSize);
	}
	
}

void DeferredRender::CreateDSV()
{
}

void DeferredRender::CreateRootSignature()
{
	//Init descriptor tables
	CD3DX12_DESCRIPTOR_RANGE range[3];
	//view dependent CBV
	range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	//light dependent CBV
	range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
	//G-Buffer inputs
	range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

	CD3DX12_ROOT_PARAMETER rootParameters[3];
	rootParameters[0].InitAsDescriptorTable(1, &range[0], D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsDescriptorTable(1, &range[1], D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[2].InitAsDescriptorTable(1, &range[2], D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
	descRootSignature.Init(3, rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	CD3DX12_STATIC_SAMPLER_DESC StaticSamplers[1];
	StaticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
	descRootSignature.NumStaticSamplers = 1;
	descRootSignature.pStaticSamplers = StaticSamplers;


	ComPtr<ID3DBlob> rootSigBlob, errorBlob;

	ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, rootSigBlob.GetAddressOf(), errorBlob.GetAddressOf()));
	ThrowIfFailed(m_device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.GetAddressOf())));

}

void DeferredRender::CreateGBufferPSO()
{
}

void DeferredRender::CreateLightPSO()
{
}

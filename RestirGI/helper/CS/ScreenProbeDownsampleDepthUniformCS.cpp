#include "helper/CS/ScreenProbeDownsampleDepthUniformCS.h"


SPDownSample::SPDownSample(ID3D12Device5* device, UINT width, UINT height, DXGI_FORMAT format)
{
	md3dDevice = device;

	mWidth = width;
	mHeight = height;
	mFormat = format;

	BuildResources();
}

void SPDownSample::BuildCSDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
	UINT descriptorSize)
{
	// Save references to the descriptors. 
	mSceneDepthCpuSrv = hCpuDescriptor;
	mSceneDepthCpuUav = hCpuDescriptor.Offset(1, descriptorSize);
	mWorldSpeedCpuSrv = hCpuDescriptor.Offset(1, descriptorSize);
	mWorldSpeedCpuUav = hCpuDescriptor.Offset(1, descriptorSize);

	mSceneDepthGpuSrv = hGpuDescriptor;
	mSceneDepthGpuUav = hGpuDescriptor.Offset(1, descriptorSize);
	mWorldSpeedGpuSrv = hGpuDescriptor.Offset(1, descriptorSize);
	mWorldSpeedGpuUav = hGpuDescriptor.Offset(1, descriptorSize);

	BuildDescriptors();
}

void SPDownSample::Execute(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* PSO, ID3D12Resource* input, ID3D12DescriptorHeap* rtSrvUavHeap)
{
	cmdList->SetComputeRootSignature(rootSig);
	cmdList->SetPipelineState(PSO);

	//cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(input,
	//	D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));

	//cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(ScreenProbeSceneDepth.Get(),
	//	D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

	//// Copy the input (back-buffer in this example) to BlurMap0.
	//cmdList->CopyResource(ScreenProbeSceneDepth.Get(), input);

	//cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(ScreenProbeSceneDepth.Get(),
	//	D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

	//cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(ScreenProbeWorldSpeed.Get(),
	//	D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	

	CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart());

	cmdList->SetComputeRootDescriptorTable(0, srvHandle);
	cmdList->SetComputeRootDescriptorTable(1, srvHandle);
	cmdList->SetComputeRootDescriptorTable(2, srvHandle);

	cmdList->Dispatch(1200, 800, 1);

	//cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(ScreenProbeWorldSpeed.Get(),
	//	D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));

	//cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(input,
	//	D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	//cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(ScreenProbeSceneDepth.Get(),
	//	D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COMMON));

}

void SPDownSample::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	uavDesc.Format = mFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	
	//¸ü»»¸ñÊ½
	srvDesc.Format = DepthFormat;
	uavDesc.Format = DepthFormat;
	md3dDevice->CreateShaderResourceView(ScreenProbeSceneDepth.Get(), &srvDesc, mSceneDepthCpuSrv);
	md3dDevice->CreateUnorderedAccessView(ScreenProbeSceneDepth.Get(), nullptr, &uavDesc, mSceneDepthCpuUav);

	srvDesc.Format = SpeedFormat;
	uavDesc.Format = SpeedFormat;
	md3dDevice->CreateShaderResourceView(ScreenProbeWorldSpeed.Get(), &srvDesc, mWorldSpeedCpuSrv);
	md3dDevice->CreateUnorderedAccessView(ScreenProbeWorldSpeed.Get(), nullptr, &uavDesc, mWorldSpeedCpuUav);
}

void SPDownSample::BuildResources()
{
	// Note, compressed formats cannot be used for UAV.  We get error like:
// ERROR: ID3D11Device::CreateTexture2D: The format (0x4d, BC3_UNORM) 
// cannot be bound as an UnorderedAccessView, or cast to a format that
// could be bound as an UnorderedAccessView.  Therefore this format 
// does not support D3D11_BIND_UNORDERED_ACCESS.

	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	texDesc.Format = DepthFormat;
	md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&ScreenProbeSceneDepth));

	texDesc.Format = SpeedFormat;
	md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&ScreenProbeWorldSpeed));
}

#include "stdafx.h"
#include "RestirGI.h"
#include <iomanip>
#include "helper/BottomLevelASGenerator.h"
#include "helper/PipelineGenerator.h"

#include "glm/gtc/type_ptr.hpp"
#include "helper/manipulator.h"
#include "helper/ModelLoader.h"
#include <windowsx.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>

#include <iostream>
#include "helper/TextureLoader.h"
#include "core/D3DUtility.h"


#define REVERSE_Z
//#define DEBUGSHADER
//#define USE_TAA_JITTER
#define PROBE_RESOLUTION 16.0
#define SCREEN_PROBE_RESOLUTION 8
#define ADAPTIVE_PROBE_ALLOCATION_FRACTION 1.5



using namespace DirectX;

static std::uniform_real_distribution<float> s_distribution(0.f, 1.f);

RestirGI::RestirGI(UINT width, UINT height, std::wstring name)
	: DXSample(width, height, name), m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_rtvDescriptorSize(0) {}

void RestirGI::OnInit()
{
	probeTexSize.x = ceil(GetWidth() / PROBE_RESOLUTION);
	probeTexSize.y = ceil(ceil(GetHeight() / PROBE_RESOLUTION)* ADAPTIVE_PROBE_ALLOCATION_FRACTION);

	//init camera
	nv_helpers_dx12::CameraManip.setWindowSize(GetWidth(), GetHeight());
	//	nv_helpers_dx12::CameraManip.setLookat(glm::vec3(0, 0.f, 0.f), glm::vec3(5, 0, 0), glm::vec3(0, 1, 0));
	nv_helpers_dx12::CameraManip.setLookat(glm::vec3(0, 15.f, 0.f), glm::vec3(5, 15, 0), glm::vec3(0, 1, 0));
	nv_helpers_dx12::CameraManip.setSun(glm::vec3(m_lightPos.x, m_lightPos.y, m_lightPos.z));

#ifdef REVERSE_Z
	m_dsvFormat = DXGI_FORMAT_D32_FLOAT;
#endif

	Initialize();
	CheckRaytracingSupport();


	LoadAssets();



	//Create Buffer
	CreateAllConstantBuffer();



	CreateAccelerationStructures();
	CreateInstancePropertiesBuffer();

	CreateRayTracingResource();
	CreateGBufferResource();

	CreateRaytracingPipeline();

	CreateCommonRasterPipeline();

	CreateShaderBindingTable();

	m_imGuiImporter = new ImGuiImporter();
	m_imGuiImporter->Initialize(Win32Application::GetHwnd(), FrameCount, DXGI_FORMAT_R8G8B8A8_UNORM, m_device.Get(), m_commandList.Get());




	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* cmdsLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	WaitForPreviousFrame();
}

void RestirGI::OnUpdate()
{
	UpdateAllConstantBuffer();
	//CalculateFrameStats();
}


void RestirGI::OnRender()
{
	m_imGuiImporter->Prepare();
	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_rasterPiplineState.Get()));

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rasterRootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);





	auto ReadResource = [this](std::vector<std::string> name) {
		auto State_CommonToRead = [this](std::vector<std::string>& name) {
			std::vector<CD3DX12_RESOURCE_BARRIER> transitionArrs;
			for (auto& i : name)
			{
				CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
					m_rtBuffer[TexMap[i].resourceBuffer].Get(),
					D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
				);
				transitionArrs.push_back(transition);
			}
			if (transitionArrs.size() > 0)
				m_commandList->ResourceBarrier(transitionArrs.size(), transitionArrs.data());
		};
		auto State_WriteToRead = [this](std::vector<std::string>& name) {
			std::vector<CD3DX12_RESOURCE_BARRIER> transitionArrs;
			for (auto& i : name)
			{
				CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
					m_rtBuffer[TexMap[i].resourceBuffer].Get(),
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
				);
				transitionArrs.push_back(transition);
			}
			if (transitionArrs.size() > 0)
				m_commandList->ResourceBarrier(transitionArrs.size(), transitionArrs.data());
		};

		std::vector<std::string> commonToRead;
		std::vector<std::string> writeToRead;
		for (const auto& i : name)
		{
			if (TexState.find(i) != TexState.end())
			{
				if (TexState[i] == ResourceState::WRITE) //如果纹理上一次的状态为可写，就改为可读；否则不变
				{
					writeToRead.push_back(i);
					TexState[i] = ResourceState::READ;
				}
			}
			else //第一次加入的纹理默认为common态
			{
				commonToRead.push_back(i);
				TexState.insert({ i,ResourceState::READ });
			}
		}
		State_CommonToRead(commonToRead);
		State_WriteToRead(writeToRead);
	};

	auto WriteResource = [this](std::vector<std::string> name) {
		auto State_UAVBarrier = [this](std::vector<std::string>& name) {
			std::vector<CD3DX12_RESOURCE_BARRIER> transitionArrs;
			for (auto& i : name)
			{
				CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::UAV(m_rtBuffer[TexMap[i].resourceBuffer].Get());
				transitionArrs.push_back(transition);
			}
			if (transitionArrs.size() > 0)
				m_commandList->ResourceBarrier(transitionArrs.size(), transitionArrs.data());
		};
		auto State_CommonToWrite = [this](std::vector<std::string>& name) {
			std::vector<CD3DX12_RESOURCE_BARRIER> transitionArrs;
			for (auto& i : name)
			{
				CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
					m_rtBuffer[TexMap[i].resourceBuffer].Get(),
					D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				);
				transitionArrs.push_back(transition);
			}
			if (transitionArrs.size() > 0)
				m_commandList->ResourceBarrier(transitionArrs.size(), transitionArrs.data());
		};
		auto State_ReadToWrite = [this](std::vector<std::string>& name) {
			std::vector<CD3DX12_RESOURCE_BARRIER> transitionArrs;
			for (auto& i : name)
			{
				CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
					m_rtBuffer[TexMap[i].resourceBuffer].Get(),
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				);
				transitionArrs.push_back(transition);
			}
			if (transitionArrs.size() > 0)
				m_commandList->ResourceBarrier(transitionArrs.size(), transitionArrs.data());
		};

		std::vector<std::string> commonToWrite;
		std::vector<std::string> readToWrite;
		std::vector<std::string> writeToWrite;
		for (const auto& i : name)
		{
			if (TexState.find(i) != TexState.end())
			{
				if (TexState[i] == ResourceState::READ) //如果纹理上一次的状态为可读，就改为可写
				{
					readToWrite.push_back(i);
					TexState[i] = ResourceState::WRITE;
				}
				else if (TexState[i] == ResourceState::WRITE)//如果纹理上一次的状态为可写，则加上UAV屏障
				{
					writeToWrite.push_back(i);
				}
			}
			else //第一次加入的纹理默认为common态
			{
				commonToWrite.push_back(i);
				TexState.insert({ i,ResourceState::WRITE });
			}
		}
		State_CommonToWrite(commonToWrite);
		State_ReadToWrite(readToWrite);
		State_UAVBarrier(writeToWrite);
	};

	auto EndResource = [this]() {
		std::vector<std::string> name;
		std::map<std::string, ResourceState>::iterator iter;
		for (iter = TexState.begin(); iter != TexState.end(); iter++)
		{
			if (iter->second == ResourceState::WRITE)
				name.push_back(iter->first);
		}

		std::vector<CD3DX12_RESOURCE_BARRIER> transitionArrs;
		for (const auto& i : name)
		{
			CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
				m_rtBuffer[TexMap[i].resourceBuffer].Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
			);
			TexState[i] = ResourceState::READ;
			transitionArrs.push_back(transition);
		}
		if (transitionArrs.size() > 0)
			m_commandList->ResourceBarrier(transitionArrs.size(), transitionArrs.data());
	};


	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_commandList->ResourceBarrier(1, &transition);

	std::vector< ID3D12DescriptorHeap* > heaps = { m_rtSrvUavHeap.Get() };
	m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	auto closeCommandList = [this, ppCommandLists, heaps]() {
		ThrowIfFailed(m_commandList->Close());

		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		m_commandList->Reset(m_commandAllocator.Get(), nullptr);
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRect);
		m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
	};
	if (m_raster) {
		if (m_showRenderMode == ShowRenderMode::DEFERRED_RENDERING) {
			m_commandList->SetPipelineState(m_rasterGBufferPSO.Get());

			const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
			rtvHandle.Offset(FrameCount, m_rtvDescriptorSize);

			for (int i = 0; i < RasterGBufferCount; i++) {
				m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
				rtvHandle.Offset(1, m_rtvDescriptorSize);
			}
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
#ifndef REVERSE_Z
			m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
#else
			m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
#endif

			rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), FrameCount, m_rtvDescriptorSize);
			m_commandList->OMSetRenderTargets(RasterGBufferCount, &rtvHandle, true, &dsvHandle);
		}
		else {
			m_commandList->SetPipelineState(m_rasterPiplineState.Get());
			//m_commandList->SetPipelineState(m_rasterGBufferPSO.Get());
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
			m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
			const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
			m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}

		//render
		{
			m_commandList->SetGraphicsRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->SetGraphicsRootConstantBufferView(1, m_rasterObjectCB->GetGPUVirtualAddress());
			for (auto& mesh : m_sceneModel.Meshes) {
				if (!mesh.second.empty()) {
					CD3DX12_GPU_DESCRIPTOR_HANDLE srvTexHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
					srvTexHandle.Offset(m_sceneModel.Textures[mesh.second[0]]->SrvHeapIndex, m_cbvSrvUavDescriptorSize);
					m_commandList->SetGraphicsRootDescriptorTable(2, srvTexHandle);
				}

				D3D12_VERTEX_BUFFER_VIEW vertexBufferView = mesh.first->VertexBufferView();
				m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
				D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.first->IndexBufferView();
				m_commandList->IASetIndexBuffer(&indexBufferView);
				m_commandList->DrawIndexedInstanced(mesh.first->IndexCount, 1, 0, 0, 0);
			}
		}

		//second pass
		if (m_showRenderMode == ShowRenderMode::DEFERRED_RENDERING) {
			m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			m_commandList->SetPipelineState(m_rasterLightPSO.Get());

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
			//CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
			m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

			const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
			m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			//m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			for (int i = 0; i < RasterGBufferCount; i++) {
				transition = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[i].Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
				m_commandList->ResourceBarrier(1, &transition);
			}
			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_dsBuffer.Get(),
				D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ);
			m_commandList->ResourceBarrier(1, &transition);


			CD3DX12_GPU_DESCRIPTOR_HANDLE srvTexHandle(m_rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), 10, m_cbvSrvUavDescriptorSize);
			m_commandList->SetGraphicsRootDescriptorTable(3, srvTexHandle);

			D3D12_VERTEX_BUFFER_VIEW vertexBufferView = m_meshes["lightPass"]->VertexBufferView();
			m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			m_commandList->DrawInstanced(4, 1, 0, 0);


			for (int i = 0; i < RasterGBufferCount; i++) {
				transition = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[i].Get(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
				m_commandList->ResourceBarrier(1, &transition);
			}
			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_dsBuffer.Get(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			m_commandList->ResourceBarrier(1, &transition);
		}

	}
	else { //DXR continue
		if (m_showRenderType != m_lastRenderType)
			frameTime = 0;
		//--------------------------------------------
		//First pass:generate gbuffer for raytracing
		//-------------------------------------------
		if (m_showRenderMode == ShowRenderMode::DEFERRED_RENDERING) {
			m_commandList->SetPipelineState(m_rasterGBufferPSO.Get());

			const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
			rtvHandle.Offset(FrameCount, m_rtvDescriptorSize);

			for (int i = 0; i < RasterGBufferCount; i++) {
				m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
				rtvHandle.Offset(1, m_rtvDescriptorSize);
			}
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
#ifndef REVERSE_Z
			m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
#else
			m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
#endif

			rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), FrameCount, m_rtvDescriptorSize);
			m_commandList->OMSetRenderTargets(RasterGBufferCount, &rtvHandle, true, &dsvHandle);

			//model render
			{
				m_commandList->SetGraphicsRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetGraphicsRootConstantBufferView(1, m_rasterObjectCB->GetGPUVirtualAddress());
				for (auto& mesh : m_sceneModel.Meshes) {
					if (!mesh.second.empty()) {
						CD3DX12_GPU_DESCRIPTOR_HANDLE srvTexHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
						srvTexHandle.Offset(m_sceneModel.Textures[mesh.second[0]]->SrvHeapIndex, m_cbvSrvUavDescriptorSize);
						m_commandList->SetGraphicsRootDescriptorTable(2, srvTexHandle);
					}

					D3D12_VERTEX_BUFFER_VIEW vertexBufferView = mesh.first->VertexBufferView();
					m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
					D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.first->IndexBufferView();
					m_commandList->IASetIndexBuffer(&indexBufferView);
					m_commandList->DrawIndexedInstanced(mesh.first->IndexCount, 1, 0, 0, 0);
				}
			}

			for (int i = 0; i < RasterGBufferCount; i++) {
				transition = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[i].Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
				m_commandList->ResourceBarrier(1, &transition);
			}
			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_dsBuffer.Get(),
				D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ);
			m_commandList->ResourceBarrier(1, &transition);
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart());

		//公有初始化
		{
			//brdf_fg
			m_commandList->SetComputeRootSignature(brdfFgCS->RootSignature.Get());
			m_commandList->SetPipelineState(brdfFgCS->PiplineState.Get());
			ReadResource(brdfFgCS->srvResource);
			WriteResource(brdfFgCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->Dispatch(8, 8, 1);

			ReadResource(brdfFgCS->uavResource); //后面只会读取该纹理

			//sky cube
			m_commandList->SetComputeRootSignature(skyCubeCS->RootSignature.Get());
			m_commandList->SetPipelineState(skyCubeCS->PiplineState.Get());
			ReadResource(skyCubeCS->srvResource);
			WriteResource(skyCubeCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootConstantBufferView(1, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->Dispatch(8, 8, 6);

			//convolve sky
			m_commandList->SetComputeRootSignature(convolveSkyCS->RootSignature.Get());
			m_commandList->SetPipelineState(convolveSkyCS->PiplineState.Get());
			ReadResource(convolveSkyCS->srvResource);
			WriteResource(convolveSkyCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
			m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->Dispatch(2, 2, 6);

			//reprojection map
			m_commandList->SetComputeRootSignature(reprojectionMapCS->RootSignature.Get());
			m_commandList->SetPipelineState(reprojectionMapCS->PiplineState.Get());

			ReadResource(reprojectionMapCS->srvResource);
			WriteResource(reprojectionMapCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
			m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

			m_commandList->Dispatch(240, 135, 1);

			//copy depth
			m_commandList->SetComputeRootSignature(copyDepthCS->RootSignature.Get());
			m_commandList->SetPipelineState(copyDepthCS->PiplineState.Get());

			ReadResource(copyDepthCS->srvResource);
			WriteResource(copyDepthCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, srvHandle);

			m_commandList->Dispatch(240, 135, 1);

			//raster
			{

				//extract view normal/2
				m_commandList->SetComputeRootSignature(extractViewNormalCS->RootSignature.Get());
				m_commandList->SetPipelineState(extractViewNormalCS->PiplineState.Get());

				ReadResource(extractViewNormalCS->srvResource);
				WriteResource(extractViewNormalCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

				m_commandList->Dispatch(120, 68, 1);

				//extract half depth
				m_commandList->SetComputeRootSignature(extractHalfDepthCS->RootSignature.Get());
				m_commandList->SetPipelineState(extractHalfDepthCS->PiplineState.Get());

				ReadResource(extractHalfDepthCS->srvResource);
				WriteResource(extractHalfDepthCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

				m_commandList->Dispatch(120, 68, 1);

				//ssao
				m_commandList->SetComputeRootSignature(ssaoCS->RootSignature.Get());
				m_commandList->SetPipelineState(ssaoCS->PiplineState.Get());

				ReadResource(ssaoCS->srvResource);
				WriteResource(ssaoCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

				m_commandList->Dispatch(120, 68, 1);

				//ssao spatial
				m_commandList->SetComputeRootSignature(ssaoSpatialCS->RootSignature.Get());
				m_commandList->SetPipelineState(ssaoSpatialCS->PiplineState.Get());

				ReadResource(ssaoSpatialCS->srvResource);
				WriteResource(ssaoSpatialCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);

				m_commandList->Dispatch(120, 68, 1);

				//ssao unsample
				m_commandList->SetComputeRootSignature(ssaoUnsampleCS->RootSignature.Get());
				m_commandList->SetPipelineState(ssaoUnsampleCS->PiplineState.Get());

				ReadResource(ssaoUnsampleCS->srvResource);
				WriteResource(ssaoUnsampleCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);

				m_commandList->Dispatch(240, 135, 1);

				if (frameTime != 0)
				{
					swapHandle("ssaoTemporal_historyTex", "ssaoTemporal_historyOutputTex");
				}
				//ssao temporal
				m_commandList->SetComputeRootSignature(ssaoTemporalCS->RootSignature.Get());
				m_commandList->SetPipelineState(ssaoTemporalCS->PiplineState.Get());

				ReadResource(ssaoTemporalCS->srvResource);
				WriteResource(ssaoTemporalCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["ssaoTemporal_historyTex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["ssaoTemporal_historyOutputTex"].uavHandle));


				m_commandList->Dispatch(240, 135, 1);

				if (frametime == 1)
				{
					swapHandle("ircache_grid_meta_buf2", "ircache_grid_meta_buf");
				}




				{
					if (!initialized)
					{
						m_commandList->SetComputeRootSignature(clearIrcachePoolCS->RootSignature.Get());
						m_commandList->SetPipelineState(clearIrcachePoolCS->PiplineState.Get());

						ReadResource(clearIrcachePoolCS->srvResource);
						WriteResource(clearIrcachePoolCS->uavResource);

						m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
						m_commandList->Dispatch(1024, 1, 1);

						initialized = true;
					}
					else
					{
						//scroll cascades
						m_commandList->SetComputeRootSignature(scrollCascadesCS->RootSignature.Get());
						m_commandList->SetPipelineState(scrollCascadesCS->PiplineState.Get());

						ReadResource(scrollCascadesCS->srvResource);
						WriteResource(scrollCascadesCS->uavResource);

						m_commandList->SetComputeRootDescriptorTable(0, srvHandle);

						m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["ircache_grid_meta_buf"].srvHandle));
						m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["ircache_grid_meta_buf2"].uavHandle));
						m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());

						m_commandList->Dispatch(1, 32, 384);//32*12

						//交换meta_buff和meta_buf2缓冲区
						swapHandle("ircache_grid_meta_buf2", "ircache_grid_meta_buf");

						frametime = (frametime + 1) % 2;
					}


					//_ircache dispatch args
					m_commandList->SetComputeRootSignature(_ircacheDispatchArgsCS->RootSignature.Get());
					m_commandList->SetPipelineState(_ircacheDispatchArgsCS->PiplineState.Get());

					ReadResource(_ircacheDispatchArgsCS->srvResource);
					WriteResource(_ircacheDispatchArgsCS->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
					m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1, 1, 1);

					//age ircache entries   
					m_commandList->SetComputeRootSignature(ageIrcacheEntriesCS->RootSignature.Get());
					m_commandList->SetPipelineState(ageIrcacheEntriesCS->PiplineState.Get());


					WriteResource(ageIrcacheEntriesCS->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
					m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1024, 1, 1); //dispatch_indirect

					//_prefix scan 1
					m_commandList->SetComputeRootSignature(_prefixScan1CS->RootSignature.Get());
					m_commandList->SetPipelineState(_prefixScan1CS->PiplineState.Get());

					WriteResource(_prefixScan1CS->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootConstantBufferView(1, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1024, 1, 1);

					//_prefix scan 2
					m_commandList->SetComputeRootSignature(_prefixScan2CS->RootSignature.Get());
					m_commandList->SetPipelineState(_prefixScan2CS->PiplineState.Get());

					ReadResource(_prefixScan2CS->srvResource);
					WriteResource(_prefixScan2CS->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
					m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1, 1, 1);

					//_prefix scan merge
					m_commandList->SetComputeRootSignature(_prefixScanMergeCS->RootSignature.Get());
					m_commandList->SetPipelineState(_prefixScanMergeCS->PiplineState.Get());

					ReadResource(_prefixScanMergeCS->srvResource);
					WriteResource(_prefixScanMergeCS->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
					m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1024, 1, 1);

					//ircache compact
					m_commandList->SetComputeRootSignature(ircacheCompactCS->RootSignature.Get());
					m_commandList->SetPipelineState(ircacheCompactCS->PiplineState.Get());

					ReadResource(ircacheCompactCS->srvResource);
					WriteResource(ircacheCompactCS->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
					m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1024, 1, 1); //dispatch_indirect

					//_ircache dispatch args
					m_commandList->SetComputeRootSignature(_ircacheDispatchArgsCS2->RootSignature.Get());
					m_commandList->SetPipelineState(_ircacheDispatchArgsCS2->PiplineState.Get());

					ReadResource(_ircacheDispatchArgsCS2->srvResource);
					WriteResource(_ircacheDispatchArgsCS2->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootConstantBufferView(1, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1, 1, 1);

					//ircache reset
					m_commandList->SetComputeRootSignature(ircacheResetCS->RootSignature.Get());
					m_commandList->SetPipelineState(ircacheResetCS->PiplineState.Get());

					ReadResource(ircacheResetCS->srvResource);
					WriteResource(ircacheResetCS->uavResource);

					m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
					m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
					m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

					m_commandList->Dispatch(1024, 1, 1); //dispatch_indirect

				}
			}

			//trace ircache
			{
				//ircache trace access
				ircacheTraceAccessRT->bindDesc(m_commandList, 65536 * 16, 1, 1);

				ReadResource(ircacheTraceAccessRT->srv);
				WriteResource(ircacheTraceAccessRT->uav);
				m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(1));
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(0));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(0));

				m_commandList->DispatchRays(&ircacheTraceAccessRT->desc);

				//ircache validate
				ircacheVaildateRT->bindDesc(m_commandList, 65536, 1, 1);

				ReadResource(ircacheVaildateRT->srv);
				WriteResource(ircacheVaildateRT->uav);
				m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(1));
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(0));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(0));

				m_commandList->DispatchRays(&ircacheVaildateRT->desc);


				//ircache trace
				ircacheTraceRT->bindDesc(m_commandList, 65536, 1, 1);

				ReadResource(ircacheTraceRT->srv);
				WriteResource(ircacheTraceRT->uav);
				m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(1));
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(0));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(0));

				m_commandList->DispatchRays(&ircacheTraceRT->desc);
			}

		}

		//sun shadow mask
		{
			traceShadowMaskRT->bindDesc(m_commandList, GetWidth(), GetHeight(), 1);

			ReadResource(traceShadowMaskRT->srv);
			WriteResource(traceShadowMaskRT->uav);
			m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(1));
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(0));
			m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(0));

			m_commandList->DispatchRays(&traceShadowMaskRT->desc);



			//shadow bitpack
			m_commandList->SetComputeRootSignature(shadowBitpackCS->RootSignature.Get());
			m_commandList->SetPipelineState(shadowBitpackCS->PiplineState.Get());

			ReadResource(shadowBitpackCS->srvResource);
			WriteResource(shadowBitpackCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, srvHandle);

			m_commandList->Dispatch(60, 68, 1);

			if (frameTime != 0) //第一次不进行交换
			{
				swapHandle("moments_image", "prev_moments_image");
				swapHandle("accum_image", "prev_accum_image");
			}

			//shadow temporal
			m_commandList->SetComputeRootSignature(shadowTemporalCS->RootSignature.Get());
			m_commandList->SetPipelineState(shadowTemporalCS->PiplineState.Get());

			ReadResource(shadowTemporalCS->srvResource);
			WriteResource(shadowTemporalCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["prev_moments_image"].srvHandle));
			m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["prev_accum_image"].srvHandle));
			m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["moments_image"].uavHandle));
			m_commandList->SetComputeRootConstantBufferView(5, m_frameBuffer->Get()->GetGPUVirtualAddress());

			m_commandList->Dispatch(240, 135, 1);

			//shadow spatial 1
			m_commandList->SetComputeRootSignature(shadowSpatialCS->RootSignature.Get());
			m_commandList->SetPipelineState(shadowSpatialCS->PiplineState.Get());
			ReadResource(std::vector<std::string>{"metadata_image", "denoised_shadow_mask"});
			WriteResource(std::vector<std::string>{"accum_image"});
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["denoised_shadow_mask"].srvHandle));
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["accum_image"].uavHandle));
			m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->SetComputeRootConstantBufferView(4, m_shadowSpatialCB1->Get()->GetGPUVirtualAddress());

			m_commandList->Dispatch(240, 135, 1);

			//shadow spatial 2
			ReadResource(std::vector<std::string>{"metadata_image", "accum_image"});
			WriteResource(std::vector<std::string>{"temp"});
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["accum_image"].srvHandle));
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["temp"].uavHandle));
			m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->SetComputeRootConstantBufferView(4, m_shadowSpatialCB2->Get()->GetGPUVirtualAddress());

			m_commandList->Dispatch(240, 135, 1);

			//shadow spatial 3
			ReadResource(std::vector<std::string>{"metadata_image", "temp"});
			WriteResource(std::vector<std::string>{"denoised_shadow_mask"});
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["temp"].srvHandle));
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["denoised_shadow_mask"].uavHandle));
			m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->SetComputeRootConstantBufferView(4, m_shadowSpatialCB3->Get()->GetGPUVirtualAddress());

			m_commandList->Dispatch(240, 135, 1);
		}
		
		if(m_showRenderType == ShowRenderType::LUMEN)
		{
			 
			{
				//ScreenProbeDownSampleSceneDepth
				m_commandList->SetComputeRootSignature(screenProbeDownSampleDepthCS->RootSignature.Get());
				m_commandList->SetPipelineState(screenProbeDownSampleDepthCS->PiplineState.Get());
				ReadResource(screenProbeDownSampleDepthCS->srvResource);
				WriteResource(screenProbeDownSampleDepthCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

				m_commandList->Dispatch(ceil((float)probeTexSize.x / SCREEN_PROBE_RESOLUTION), ceil((float)probeTexSize.y / SCREEN_PROBE_RESOLUTION), 1);

				//clearProbe
				m_commandList->SetComputeRootSignature(clearProbeCS->RootSignature.Get());
				m_commandList->SetPipelineState(clearProbeCS->PiplineState.Get());
				ReadResource(clearProbeCS->srvResource);
				WriteResource(clearProbeCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->Dispatch(240, 270, 1);

				//Adaptive screen probe placement
				m_commandList->SetComputeRootSignature(screenProbeAdaptivePlacementCS->RootSignature.Get());
				m_commandList->SetPipelineState(screenProbeAdaptivePlacementCS->PiplineState.Get());
				ReadResource(screenProbeAdaptivePlacementCS->srvResource);
				WriteResource(screenProbeAdaptivePlacementCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(3, m_screenProbeAdaptivePlacementCB1->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(ceil((float)m_width/8/8), ceil((float)m_height / 8 / 8), 1); // 30 * 8 * 8 =  1920, 16.875 * 8 * 8 = 1080
				

				ReadResource(screenProbeAdaptivePlacementCS->srvResource);
				WriteResource(screenProbeAdaptivePlacementCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(3, m_screenProbeAdaptivePlacementCB2->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(ceil((float)m_width / 8 / 4), ceil((float)m_height / 8 / 4), 1); // 60 * 8 * 4 =  1920, 33.75 * 8 *4 = 1080


			}
			
			if (frameTime != 0) //第一次不进行交换
			{
				swapHandle("temporal_history_tex", "temporal_output_tex");
			}
			//reproject
			m_commandList->SetComputeRootSignature(rtdgiReprojectCS->RootSignature.Get());
			m_commandList->SetPipelineState(rtdgiReprojectCS->PiplineState.Get());

			ReadResource(rtdgiReprojectCS->srvResource);
			WriteResource(rtdgiReprojectCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["temporal_history_tex"].srvHandle));
			m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());

			m_commandList->Dispatch(240, 135, 1);

			//validate probe trace
			if(EnableVaildateRay)
			{
				LumenVaildateRT->bindDesc(m_commandList, probeTexSize.x* probeTexSize.y, SCREEN_PROBE_RESOLUTION* SCREEN_PROBE_RESOLUTION, 1);

				ReadResource(LumenVaildateRT->srv);
				WriteResource(LumenVaildateRT->uav);
				m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(1, m_indirectCB->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(1)); //acceleration struction
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["reprojected_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(4, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(5, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(6, srvHandle);

				m_commandList->DispatchRays(&LumenVaildateRT->desc);
			}


			//trace probe
			LumenRT->bindDesc(m_commandList, probeTexSize.x * probeTexSize.y , SCREEN_PROBE_RESOLUTION * SCREEN_PROBE_RESOLUTION, 1);

			ReadResource(LumenRT->srv);
			WriteResource(LumenRT->uav);
			m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->SetComputeRootConstantBufferView(1, m_indirectCB->Get()->GetGPUVirtualAddress());
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(1)); //acceleration struction
			m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["reprojected_history_tex"].srvHandle));
			m_commandList->SetComputeRootDescriptorTable(4, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(5, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
			m_commandList->SetComputeRootDescriptorTable(6, srvHandle);

			m_commandList->DispatchRays(&LumenRT->desc);

			
			{
				//filter
				m_commandList->SetComputeRootSignature(screenProbeFilterCS->RootSignature.Get());
				m_commandList->SetPipelineState(screenProbeFilterCS->PiplineState.Get());
				ReadResource(screenProbeFilterCS->srvResource);
				WriteResource(screenProbeFilterCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(3, m_indirectCB->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(probeTexSize.x, probeTexSize.y, 1); 

				//convert to SH
				m_commandList->SetComputeRootSignature(screenProbeConvertToSHCS->RootSignature.Get());
				m_commandList->SetPipelineState(screenProbeConvertToSHCS->PiplineState.Get());
				ReadResource(screenProbeConvertToSHCS->srvResource);
				WriteResource(screenProbeConvertToSHCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(probeTexSize.x, 1, 1); // 

				// Indirect CS
				
				m_commandList->SetComputeRootSignature(indirectCS->RootSignature.Get());
				m_commandList->SetPipelineState(indirectCS->PiplineState.Get());
				ReadResource(indirectCS->srvResource);
				WriteResource(indirectCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(3, m_indirectCB->Get()->GetGPUVirtualAddress());
				
				m_commandList->Dispatch(240, 270, 1);

				if (frameTime != 0)
				{
					swapHandle("temporal_variance_output_tex_Lumen", "variance_history_tex_Lumen");
				}

				//temporal
				m_commandList->SetComputeRootSignature(temporalCS->RootSignature.Get());
				m_commandList->SetPipelineState(temporalCS->PiplineState.Get());

				ReadResource(temporalCS->srvResource);
				WriteResource(temporalCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["variance_history_tex_Lumen"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["rt_history_validity_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["temporal_output_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(5, GetGPUHandle(TexMap["temporal_variance_output_tex_Lumen"].uavHandle));

				m_commandList->SetComputeRootConstantBufferView(6, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(7, m_indirectCB->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(240, 135, 1);

				//light gbuffer
				m_commandList->SetComputeRootSignature(lightCS->RootSignature.Get());
				m_commandList->SetPipelineState(lightCS->PiplineState.Get());

				ReadResource(lightCS->srvResource);
				WriteResource(lightCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(3, m_indirectCB->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(4, srvHandle);
				m_commandList->Dispatch(240, 135, 1);


			}
		}



		static bool first_path = true;
		if(m_showRenderType == ShowRenderType::PATH_TRACTING)
		{
			
			//clear
			if (isMouseMove || isKeyboardMove || first_path)
			{
				m_commandList->SetComputeRootSignature(clearPathTraceCS->RootSignature.Get());
				m_commandList->SetPipelineState(clearPathTraceCS->PiplineState.Get());
				ReadResource(clearPathTraceCS->srvResource);
				WriteResource(clearPathTraceCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->Dispatch(240, 135, 1);
				first_path = false;
			}


			pathTraceRT->bindDesc(m_commandList, GetWidth() , GetHeight() , 1);

			ReadResource(pathTraceRT->srv);
			WriteResource(pathTraceRT->uav);
			m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
			m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(1)); //acceleration struction
			m_commandList->SetComputeRootDescriptorTable(2, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(3, srvHandle);

			m_commandList->DispatchRays(&pathTraceRT->desc);
		}



		//Restir GI
		if(m_showRenderType != ShowRenderType::PATH_TRACTING && m_showRenderType != ShowRenderType::LUMEN)
		{
			first_path = true;





			if (frameTime != 0) //第一次不进行交换
			{
				swapHandle("temporal_history_tex", "temporal_output_tex");
			}
			//rtdgi reproject
			m_commandList->SetComputeRootSignature(rtdgiReprojectCS->RootSignature.Get());
			m_commandList->SetPipelineState(rtdgiReprojectCS->PiplineState.Get());

			ReadResource(rtdgiReprojectCS->srvResource);
			WriteResource(rtdgiReprojectCS->uavResource);
			m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
			m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["temporal_history_tex"].srvHandle));
			m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());

			m_commandList->Dispatch(240, 135, 1);
			


			{
				//ircache sum
				m_commandList->SetComputeRootSignature(ircacheSumCS->RootSignature.Get());
				m_commandList->SetPipelineState(ircacheSumCS->PiplineState.Get());

				ReadResource(ircacheSumCS->srvResource);
				WriteResource(ircacheSumCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());

				m_commandList->Dispatch(1024, 1, 1); //dispatch_indirect

				//extract ssao/2
				m_commandList->SetComputeRootSignature(extractHalfSSaoCS->RootSignature.Get());
				m_commandList->SetPipelineState(extractHalfSSaoCS->PiplineState.Get());

				ReadResource(extractHalfSSaoCS->srvResource);
				WriteResource(extractHalfSSaoCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(120, 68, 1);

			}

			//rtdgi validate
			if (frameTime != 0) //第一次不进行交换
			{
				swapHandle("hit_normal_history_tex", "hit_normal_output_tex");
				swapHandle("candidate_history_tex", "candidate_output_tex");
				swapHandle("invalidity_history_tex", "invalidity_output_tex");
				swapHandle("ray_history_tex", "ray_output_tex");
				swapHandle("ray_orig_history_tex", "ray_orig_output_tex");
				swapHandle("reservoir_history_tex", "reservoir_output_tex");
				swapHandle("radiance_history_tex", "radiance_output_tex");
			}


			{
				rtdgiValidateRT->bindDesc(m_commandList, GetWidth() / 2, GetHeight() / 2, 1);

				ReadResource(rtdgiValidateRT->srv);
				WriteResource(rtdgiValidateRT->uav);
				m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(1)); //acceleration struction
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["reprojected_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["ray_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["ray_orig_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(5, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(6, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(7, GetGPUHandle(TexMap["reservoir_history_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(8, GetGPUHandle(TexMap["radiance_history_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(9, srvHandle);

				m_commandList->DispatchRays(&rtdgiValidateRT->desc);
			}

			//rtdgi trace
			{
				rtdgiTraceRT->bindDesc(m_commandList, GetWidth() / 2, GetHeight() / 2, 1);

				ReadResource(rtdgiTraceRT->srv);
				WriteResource(rtdgiTraceRT->uav);
				m_commandList->SetComputeRootConstantBufferView(0, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(1)); //acceleration struction
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["ray_orig_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(5, srvHandle);

				m_commandList->DispatchRays(&rtdgiTraceRT->desc);
			}


			{

				//validity integrate
				m_commandList->SetComputeRootSignature(validityIntegrateCS->RootSignature.Get());
				m_commandList->SetPipelineState(validityIntegrateCS->PiplineState.Get());

				ReadResource(validityIntegrateCS->srvResource);
				WriteResource(validityIntegrateCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["invalidity_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["invalidity_output_tex"].uavHandle));
				m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(120, 68, 1);

				//restir temporal
				m_commandList->SetComputeRootSignature(restirTemporalCS->RootSignature.Get());
				m_commandList->SetPipelineState(restirTemporalCS->PiplineState.Get());

				ReadResource(restirTemporalCS->srvResource);
				WriteResource(restirTemporalCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["radiance_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["ray_orig_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["ray_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(5, GetGPUHandle(TexMap["reservoir_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(6, GetGPUHandle(TexMap["hit_normal_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(7, GetGPUHandle(TexMap["candidate_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(8, GetGPUHandle(TexMap["invalidity_output_tex"].srvHandle));

				m_commandList->SetComputeRootDescriptorTable(9, GetGPUHandle(TexMap["radiance_output_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(10, GetGPUHandle(TexMap["ray_orig_output_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(11, GetGPUHandle(TexMap["ray_output_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(12, GetGPUHandle(TexMap["hit_normal_output_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(13, GetGPUHandle(TexMap["reservoir_output_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(14, GetGPUHandle(TexMap["candidate_output_tex"].uavHandle));
				m_commandList->SetComputeRootConstantBufferView(15, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(16, srvHandle);
				m_commandList->Dispatch(120, 68, 1);

				//restir spatial 1

				m_commandList->SetComputeRootSignature(restirSpatialCS->RootSignature.Get());
				m_commandList->SetPipelineState(restirSpatialCS->PiplineState.Get());

				ReadResource(restirSpatialCS->srvResource);
				WriteResource(restirSpatialCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["reservoir_output_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["radiance_output_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["reservoir_output_tex0"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["bounced_radiance_output_tex0"].uavHandle));
				m_commandList->SetComputeRootConstantBufferView(5, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(6, m_restirSpatialCB1->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(7, srvHandle);
				m_commandList->Dispatch(120, 68, 1);


				//restir spatial 2
				m_commandList->SetComputeRootSignature(restirSpatialCS->RootSignature.Get());
				m_commandList->SetPipelineState(restirSpatialCS->PiplineState.Get());

				ReadResource(std::vector<std::string>{"half_view_normal_tex", "half_depth_tex", "half_ssao_tex", "temporal_reservoir_packed_tex", "reprojected_history_tex",
					"reservoir_output_tex0", "bounced_radiance_output_tex0"});
				WriteResource(std::vector<std::string>{"reservoir_output_tex1", "bounced_radiance_output_tex1"});
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["reservoir_output_tex0"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["bounced_radiance_output_tex0"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["reservoir_output_tex1"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["bounced_radiance_output_tex1"].uavHandle));
				m_commandList->SetComputeRootConstantBufferView(5, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootConstantBufferView(6, m_restirSpatialCB2->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(7, srvHandle);
				m_commandList->Dispatch(120, 68, 1);

				//restir resolve
				m_commandList->SetComputeRootSignature(restirResolveCS->RootSignature.Get());
				m_commandList->SetPipelineState(restirResolveCS->PiplineState.Get());

				ReadResource(restirResolveCS->srvResource);
				WriteResource(restirResolveCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["radiance_output_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["reservoir_output_tex1"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["bounced_radiance_output_tex1"].srvHandle));
				m_commandList->SetComputeRootConstantBufferView(5, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(6, srvHandle);
				m_commandList->Dispatch(240, 135, 1);

				if (frameTime != 0)
				{
					swapHandle("temporal_variance_output_tex", "variance_history_tex");
				}

				//rtdgi temporal
				m_commandList->SetComputeRootSignature(rtdgiTemporalCS->RootSignature.Get());
				m_commandList->SetPipelineState(rtdgiTemporalCS->PiplineState.Get());

				ReadResource(rtdgiTemporalCS->srvResource);
				WriteResource(rtdgiTemporalCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["variance_history_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(3, GetGPUHandle(TexMap["invalidity_output_tex"].srvHandle));
				m_commandList->SetComputeRootDescriptorTable(4, GetGPUHandle(TexMap["temporal_output_tex"].uavHandle));
				m_commandList->SetComputeRootDescriptorTable(5, GetGPUHandle(TexMap["temporal_variance_output_tex"].uavHandle));

				m_commandList->SetComputeRootConstantBufferView(6, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(240, 135, 1);


				//rtdgi spatial
				m_commandList->SetComputeRootSignature(rtdgiSpatialCS->RootSignature.Get());
				m_commandList->SetPipelineState(rtdgiSpatialCS->PiplineState.Get());

				ReadResource(rtdgiSpatialCS->srvResource);
				WriteResource(rtdgiSpatialCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->Dispatch(240, 135, 1);

				//light gbuffer
				m_commandList->SetComputeRootSignature(lightGbufferCS->RootSignature.Get());
				m_commandList->SetPipelineState(lightGbufferCS->PiplineState.Get());

				ReadResource(lightGbufferCS->srvResource);
				WriteResource(lightGbufferCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(2, GetGPUHandle(TexMap["ircache_grid_meta_buf"].uavHandle));
				m_commandList->SetComputeRootConstantBufferView(3, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(4, srvHandle);
				m_commandList->Dispatch(240, 135, 1);

				//debug
				m_commandList->SetComputeRootSignature(copyTestCS->RootSignature.Get());
				m_commandList->SetPipelineState(copyTestCS->PiplineState.Get());

				ReadResource(copyTestCS->srvResource);
				WriteResource(copyTestCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);

				m_commandList->Dispatch(240, 135, 1);
			}

		}
		//post
		{
			//post combine
			
			{
				m_commandList->SetComputeRootSignature(postCombineCS->RootSignature.Get());
				m_commandList->SetPipelineState(postCombineCS->PiplineState.Get());

				ReadResource(postCombineCS->srvResource);
				WriteResource(postCombineCS->uavResource);
				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				if (m_showRenderType != ShowRenderType::PATH_TRACTING)
					m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["debug_out_tex"].srvHandle));
				else
					m_commandList->SetComputeRootDescriptorTable(1, GetGPUHandle(TexMap["path_trace_tex"].srvHandle));
				m_commandList->SetComputeRootConstantBufferView(2, m_frameBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetComputeRootDescriptorTable(3, srvHandle);
				m_commandList->Dispatch(240, 135, 1);
			}

			//RMSE
			if(false)
			{


				m_commandList->SetComputeRootSignature(calRMSECS->RootSignature.Get());
				m_commandList->SetPipelineState(calRMSECS->PiplineState.Get());

				ReadResource(calRMSECS->srvResource);
				WriteResource(calRMSECS->uavResource);

				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);

				m_commandList->Dispatch(240, 270, 1);

				m_commandList->SetComputeRootSignature(calRMSENumCS->RootSignature.Get());
				m_commandList->SetPipelineState(calRMSENumCS->PiplineState.Get());

				ReadResource(calRMSENumCS->srvResource);
				WriteResource(calRMSENumCS->uavResource);

				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);

				m_commandList->Dispatch(240, 270, 1);

			}

			//for test
			{
				CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart());

				m_commandList->SetComputeRootSignature(finalCS->RootSignature.Get());
				m_commandList->SetPipelineState(finalCS->PiplineState.Get());

				m_commandList->SetComputeRootDescriptorTable(0, srvHandle);
				m_commandList->SetComputeRootDescriptorTable(1, srvHandle);
				m_commandList->SetComputeRootConstantBufferView(2, m_finalCB->Get()->GetGPUVirtualAddress());

				m_commandList->Dispatch(240, 135, 1);
			}
		}

		EndResource();
		frameTime++;
		
		if (m_showRenderMode == ShowRenderMode::DEFERRED_RENDERING) {
			//----------------------------------------------------------
			//reset first pass resource state
			//----------------------------------------------------------
			for (int i = 0; i < RasterGBufferCount; i++) {
				transition = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[i].Get(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
				m_commandList->ResourceBarrier(1, &transition);
			}
			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_dsBuffer.Get(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			m_commandList->ResourceBarrier(1, &transition);


			//-------------------------------------------------------
			//third pass : temporal filter
			//----------------------------------------------------------
			{
				m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				m_commandList->SetPipelineState(m_rasterTemporalFilterPSO.Get());
				m_commandList->SetGraphicsRootSignature(m_rasterTemporalFilterSignature.Get());

				//CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), 6, m_rtvDescriptorSize);
				m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
				const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
				m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

				m_commandList->SetGraphicsRootConstantBufferView(0, m_temporalBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetGraphicsRootDescriptorTable(1, GetGPUHandle(20));

				transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
				m_commandList->ResourceBarrier(1, &transition);

				D3D12_VERTEX_BUFFER_VIEW vertexBufferView = m_meshes["lightPass"]->VertexBufferView();
				m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
				m_commandList->DrawInstanced(4, 1, 0, 0);

				transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				m_commandList->ResourceBarrier(1, &transition);
			}
			//--------------------------------------
			//fourth pass : temporal
			//If Tempral is false, temporal filter is used to calculate variance for svgf atrous filter, moment pass and variance pass is no need.
			//-----------------------------------------
			if (m_bTemporal)
			{

			}


			//-------------------------------------------------------
			//fifth pass : Atrous Filter
			//----------------------------------------------------------
			//notice: we must execute commandlist when we reuse render target buffer
			ThrowIfFailed(m_commandList->Close());
			ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
			m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
			{
				ComPtr<ID3D12Resource> atrousWriteTexture = nullptr, atrousReadTexture = nullptr;
				for (int i = 0; i < m_atrousFilterNum; ++i)
				{
					m_commandList->Reset(m_commandAllocator.Get(), nullptr);
					m_commandList->SetPipelineState(m_rasterAtrousFilterPSO.Get());
					m_commandList->SetGraphicsRootSignature(m_rasterAtrousFilterSignature.Get());
					m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
					m_commandList->RSSetViewports(1, &m_viewport);
					m_commandList->RSSetScissorRects(1, &m_scissorRect);
					m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

					int rtvAtrousWriteOffset; //rtv atrous write offset
					int srvAtrousReadOffset;
					if (i == 0) {
						atrousWriteTexture = m_rtBuffer[8];// atrous texture 1
						atrousReadTexture = m_rtBuffer[4]; //temporal texture( float4(color,variance) )
						rtvAtrousWriteOffset = 10;
						srvAtrousReadOffset = 50;
					}
					else {
						if (i % 2 == 0)
						{
							atrousWriteTexture = m_rtBuffer[8];// atrous texture 1
							atrousReadTexture = m_rtBuffer[7];// atrous texture 0
							rtvAtrousWriteOffset = 10;
							srvAtrousReadOffset = 60;
						}
						else
						{
							atrousWriteTexture = m_rtBuffer[7]; // atrous texture 0
							atrousReadTexture = m_rtBuffer[8];// atrous texture 1
							rtvAtrousWriteOffset = 9;
							srvAtrousReadOffset = 61;
						}
					}
					CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), rtvAtrousWriteOffset, m_rtvDescriptorSize);
					m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
					const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
					m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

					std::unique_ptr<AtrousConstantBuffer> atrousConstant = std::make_unique<AtrousConstantBuffer>();
					atrousConstant->stepWidth = (1 << (i + 1)) - 1;
					atrousConstant->colorPhi = 1.0f / i * 3.3f;
					atrousConstant->normalPhi = 1.0f / (1 << i) * (1E-2f);
					atrousConstant->posPhi = 1.0f / (1 << i) * 5.5f;
					atrousConstant->phiLuminance = m_phiLuminance;
					atrousConstant->phiNormal = m_phiNormal;
					atrousConstant->phiDepth = m_phiDepth;
					atrousConstant->atrousType = m_atrousFilterType;
					m_atrousBuffer->CopyData(0, atrousConstant.get());

					m_commandList->SetGraphicsRootConstantBufferView(0, m_atrousBuffer->Get()->GetGPUVirtualAddress());
					m_commandList->SetGraphicsRootDescriptorTable(1, GetGPUHandle(0));
					m_commandList->SetGraphicsRootDescriptorTable(2, GetGPUHandle(srvAtrousReadOffset));

					transition = CD3DX12_RESOURCE_BARRIER::Transition(atrousReadTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
					m_commandList->ResourceBarrier(1, &transition);

					D3D12_VERTEX_BUFFER_VIEW vertexBufferView = m_meshes["lightPass"]->VertexBufferView();
					m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
					m_commandList->DrawInstanced(4, 1, 0, 0);

					transition = CD3DX12_RESOURCE_BARRIER::Transition(atrousReadTexture.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
					m_commandList->ResourceBarrier(1, &transition);


					// Copy History Color
					//if (i == 0)
					//{
					//	D3D12_RESOURCE_BARRIER preCopyBarriers[2];
					//	preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[15].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
					//	preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(atrousWriteTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
					//	m_commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

					//	m_commandList->CopyResource(m_rtBuffer[15].Get(), atrousWriteTexture.Get());//save to history color

					//	D3D12_RESOURCE_BARRIER postCopyBarriers[2];
					//	postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[15].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
					//	postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(atrousWriteTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

					//	m_commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
					//}

					ThrowIfFailed(m_commandList->Close());
					ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
					m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
					WaitForPreviousFrame();

				}
				m_commandList->Reset(m_commandAllocator.Get(), nullptr);
			}



			//-------------------------------------------------------
			//six pass : post processing
			//----------------------------------------------------------
			{
				m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				m_commandList->SetPipelineState(m_rasterPostProcessingPSO.Get());
				m_commandList->SetGraphicsRootSignature(m_rasterPostProcessingSignature.Get());
				m_commandList->RSSetViewports(1, &m_viewport);
				m_commandList->RSSetScissorRects(1, &m_scissorRect);
				m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

				CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
				m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
				const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
				m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

				ComPtr<ID3D12Resource> filterResource;
				D3D12_GPU_DESCRIPTOR_HANDLE filterResult;

				if (m_atrousFilterNum == 0) {
					filterResource = m_rtBuffer[4].Get();//temporal texture
					filterResult = GetGPUHandle(50);
				}
				else if (m_atrousFilterNum % 2 == 0) {
					filterResource = m_rtBuffer[7].Get();//atrous texture 0
					filterResult = GetGPUHandle(60);
				}
				else {
					filterResource = m_rtBuffer[8].Get();//atrous texture 1
					filterResult = GetGPUHandle(61);
				}

				m_commandList->SetGraphicsRootConstantBufferView(0, m_postProcessingBuffer->Get()->GetGPUVirtualAddress());
				m_commandList->SetGraphicsRootDescriptorTable(1, filterResult);
				m_commandList->SetGraphicsRootDescriptorTable(2, GetGPUHandle(0));

				transition = CD3DX12_RESOURCE_BARRIER::Transition(filterResource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
				m_commandList->ResourceBarrier(1, &transition);

				for (int i = 0; i < RasterGBufferCount; i++) {
					transition = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[i].Get(),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
					m_commandList->ResourceBarrier(1, &transition);
				}

				D3D12_VERTEX_BUFFER_VIEW vertexBufferView = m_meshes["lightPass"]->VertexBufferView();
				m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
				m_commandList->DrawInstanced(4, 1, 0, 0);

				transition = CD3DX12_RESOURCE_BARRIER::Transition(filterResource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
				m_commandList->ResourceBarrier(1, &transition);

				for (int i = 0; i < RasterGBufferCount; i++) {
					transition = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[i].Get(),
						D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
					m_commandList->ResourceBarrier(1, &transition);
				}

				// Copy History Color
				//if (m_atrousFilterNum == 0)
				//{
				//	D3D12_RESOURCE_BARRIER preCopyBarriers[2];
				//	preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[15].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
				//	preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(filterResource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
				//	m_commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);
				//	m_commandList->CopyResource(m_rtBuffer[15].Get(), filterResource.Get());
				//	D3D12_RESOURCE_BARRIER postCopyBarriers[2];
				//	postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_rtBuffer[15].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
				//	postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(filterResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
				//	m_commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
				//}
				// Copy History Depth and Normal
				//m_rasterization->CopyDepthTexture(commandList, m_historyDepthTexture->GetTexture());
				//m_rasterization->CopyNormalTexture(commandList, m_historyNormalTexture->GetTexture());
			}


		}
		else {
			//common render
			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
			m_commandList->ResourceBarrier(1, &transition);
			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
			m_commandList->ResourceBarrier(1, &transition);

			m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_outputResource.Get());

			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
			m_commandList->ResourceBarrier(1, &transition);
			transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			m_commandList->ResourceBarrier(1, &transition);
		}


		//end
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		//DXR GUI
		{
			ImGui::Begin("Progressive Raytracing Setting");

			if (m_showRenderMode == ShowRenderMode::DEFERRED_RENDERING) {
				m_lastRenderType = m_showRenderType;

				ImGui::Text("Show Resoult Type");
				ImGui::RadioButton("Path Tracing", &m_showRenderType, ShowRenderType::PATH_TRACTING);
				ImGui::SameLine();
				ImGui::RadioButton("Restir GI", &m_showRenderType, ShowRenderType::RESTIR);
				ImGui::SameLine();
				ImGui::RadioButton("Only Direct Light", &m_showRenderType, ShowRenderType::DIRECT_LIGHT);
				ImGui::RadioButton("Only Diffuse", &m_showRenderType, ShowRenderType::DIFFUSE);
				ImGui::SameLine();
				ImGui::RadioButton("Albedo", &m_showRenderType, ShowRenderType::ALBEDO);
				ImGui::SameLine();
				ImGui::RadioButton("Depth", &m_showRenderType, ShowRenderType::DEPTH);
				ImGui::SameLine();
				ImGui::RadioButton("Normal", &m_showRenderType, ShowRenderType::NORMAL);
				ImGui::SameLine();
				//ImGui::RadioButton("AO", &m_showRenderType, ShowRenderType::AO);
				//ImGui::SameLine();
				ImGui::RadioButton("POSITION", &m_showRenderType, ShowRenderType::POSITION);
				ImGui::RadioButton("IRCACHE", &m_showRenderType, ShowRenderType::IRCACHE);
				ImGui::RadioButton("LUMEN", &m_showRenderType, ShowRenderType::LUMEN);

				ImGui::Separator();
				
				auto AddItem = [this](const char* itemName, ShowDebug type) {
				
					//ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor::HSV( 1.0f, 1.0f, 1.0f));
					//ImGui::PushStyleColor(imguitext_, (ImVec4)ImColor::HSV( 1.0f, 1.0f, 0.1f));
					if(typeLocked == int(type))
						ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.1f, 1.0f), itemName);
					else
						ImGui::Text(itemName);
					//ImGui::PopStyleColor(1);
					if (ImGui::IsItemHovered())
					{
						m_finalDebug = type;
						isHoverd = true;
						ImGui::SetColorEditOptions(ImGuiColorEditFlags_DisplayHSV);
						if (ImGui::IsItemClicked())
						{
							if (typeLocked == int(type))
								typeLocked = -1;
							else 
								typeLocked = int(type);
							

						}
					}
		

				};

				{
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Debug Texture(clip it)");

					AddItem("rtdgi validate", ShowDebug::RTDGI_VALIDATE);
					AddItem("rtdgi trace", ShowDebug::RTDGI_TRACE);
					AddItem("validity integrate", ShowDebug::VALIDITY_INTEGRATE);
					AddItem("restir temporal", ShowDebug::RESTIR_TEMPORAL);
					AddItem("restir spatial", ShowDebug::RESTIR_SPATIAL);
					AddItem("restir resolve", ShowDebug::RESTIR_RESOLVE);
					AddItem("rtdgi temporal", ShowDebug::RTDGI_TEMPORAL);
					AddItem("rtdgi spatial", ShowDebug::RTDGI_SPATIAL);
					AddItem("final", ShowDebug::FINAL);
					AddItem("test", ShowDebug::TEST);

					if (typeLocked == -1 && !isHoverd)
						m_finalDebug = ShowDebug::DEFAULT;
					else if (!isHoverd)
						m_finalDebug = ShowDebug(typeLocked);
					ImGui::Text("now is %d", m_finalDebug);
					isHoverd = false;
				}

				


				ImGui::Separator();
				ImGui::Text("Enable Function");
				ImGui::Checkbox("Enable Tone mapping", &USE_TONE_MAPPING);
				ImGui::Checkbox("On(just for debug)", &USE_shotcut);
				ImGui::Checkbox("Enable AO", &m_enableAO);
				ImGui::Checkbox("Enable Temporal", &m_bTemporal);
				ImGui::Checkbox("Enable bindless", &importsampleRayGen);
				ImGui::Checkbox("Enable SCREEN_GI_REPROJECTION", &USE_SCREEN_GI_REPROJECTION);
				ImGui::Checkbox("Only IRCACHE", &USE_IRCACHE);
				ImGui::Checkbox("Enable exposure", &USE_EXPOSURE);
				ImGui::Checkbox("Show Probe Radiance", &ShowProbeRadiance);
				ImGui::Checkbox("Open Filter", &EnableFilter);
				ImGui::Checkbox("Open Reservoir", &EnableReservoir);
				ImGui::Checkbox("Open Vaildate Ray", &EnableVaildateRay);
				ImGui::Checkbox("Open Temporal", &Enabletemporal);


				ImGui::Separator();

				//when choose ShowRenderType::AO, default choose m_enableAO = true;
				if (m_lastRenderType != m_showRenderType)
				{
					if (m_showRenderType == ShowRenderType::AO)
						m_enableAO = true;
					else
						m_enableAO = false;
				}

				ImGui::Text("Atrous Filter Type: ");
				ImGui::RadioButton("Atrous Filter", &m_atrousFilterType, 0); ImGui::SameLine();
				ImGui::RadioButton("SVGF Filter", &m_atrousFilterType, 1);
				ImGui::Text("Atrous Filter Iteration Num: ");
				//int lastNum = m_atrousFilterNum;
				int iterNum = m_atrousFilterNum;
				ImGui::SliderInt("Iteration Num", &iterNum, 0, 20);
				m_atrousFilterNum = iterNum;
				//float lastPhi = m_phiLuminance;
				ImGui::DragFloat("Phi Luminance", &m_phiLuminance, 0.1, 0.0f, 10000.0f, "%.1f");
				//lastPhi = m_phiNormal;
				ImGui::DragFloat("Phi Normal", &m_phiNormal, 0.1, 0.0f, 10000.0f, "%.1f");
				//lastPhi = m_phiDepth;
				ImGui::DragFloat("Phi Depth", &m_phiDepth, 0.01, 0.0f, 10000.0f, "%.2f");

			}

			ImGui::Separator();
			ImGui::Text("Light Type: ");
			ImGui::Text("Point Light:");
			ImGui::DragFloat("Light Power", &lightPower, 0.01, 0.0f, 100.0f, "%.2f");
			ImGui::DragFloat("Expousre", &expousre, 0.01, 0.0f, 5.0f, "%.2f");
			ImGui::DragFloat("offset", &offset, 0.01, 0.0f, 1.0f, "%.2f");
			ImGui::DragFloat("depth_thresold", &depth_thresold, 0.0001, 0.0f, 0.3f, "%.4f");

			float lPos[3];
			lPos[0] = nv_helpers_dx12::CameraManip.getSun().x;
			lPos[1] = nv_helpers_dx12::CameraManip.getSun().y;
			lPos[2] = nv_helpers_dx12::CameraManip.getSun().z;
			ImGui::DragFloat3("Light Position", lPos, 0.1);
			ImGui::Separator();

			nv_helpers_dx12::CameraManip.setSun(glm::vec3(lPos[0], lPos[1], lPos[2]));

			float origin[3];
			origin[0] = DDGIorigin.x;
			origin[1] = DDGIorigin.y;
			origin[2] = DDGIorigin.z;
			ImGui::DragFloat3("DDGI origin", origin, 0.1);

			DDGIorigin.x = origin[0];
			DDGIorigin.y = origin[1];
			DDGIorigin.z = origin[2];

			ImGui::End();
		}
	}

	//Common GUI
	{
		ImGui::Begin("Common Setting");
		ImGui::Text("Camera Setting");
		ImGui::SliderFloat("cameraMoveStep", &m_cameraMoveStep, 0.001f, 5.0f);
		ImGui::Separator();
		ImGui::Text("camera pos: %1f,%2f,%3f", nv_helpers_dx12::CameraManip.getCameraPos().x, nv_helpers_dx12::CameraManip.getCameraPos().y, nv_helpers_dx12::CameraManip.getCameraPos().z);
		ImGui::Separator();
		ImGui::Text("Frame time: %1d", frameTime);
		ImGui::Separator();
		ImGui::Text("Render Mode");
		ImGui::RadioButton("Common", &m_showRenderMode, ShowRenderMode::COMMON);
		ImGui::SameLine();
		ImGui::RadioButton("Deferred Rendering", &m_showRenderMode, ShowRenderMode::DEFERRED_RENDERING);
		ImGui::End();

		ImGui::Begin("Info");
		ImGui::Text("fps: %1f , GPU[%2d]: %3s", ImGui::GetIO().Framerate, m_adapterID, m_adpaterDescString.c_str());
		ImGui::End();

		m_imGuiImporter->Render();
	}

	transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_commandList->ResourceBarrier(1, &transition);

	ThrowIfFailed(m_commandList->Close());
	//ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(1, 0));
	WaitForPreviousFrame();
}

void RestirGI::OnDestroy()
{
	WaitForPreviousFrame();
	delete m_imGuiImporter;
	CloseHandle(m_fenceEvent);
}

void RestirGI::OnButtonDown(UINT32 lParam)
{
	nv_helpers_dx12::CameraManip.setMousePosition(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam));
}

void RestirGI::OnMouseMove(UINT8 wParam, UINT32 lParam)
{
	using nv_helpers_dx12::Manipulator;
	Manipulator::Inputs inputs;
	isMouseMove = false;
	inputs.lmb = wParam & MK_LBUTTON;
	inputs.mmb = wParam & MK_MBUTTON;
	inputs.rmb = wParam & MK_RBUTTON;
	if (!inputs.lmb && !inputs.rmb && !inputs.mmb)
		return; // no mouse button pressed

	inputs.ctrl = GetAsyncKeyState(VK_CONTROL);
	inputs.shift = GetAsyncKeyState(VK_SHIFT);
	inputs.alt = GetAsyncKeyState(VK_MENU);

	ImGuiIO& io = ImGui::GetIO();
	if (!io.WantCaptureMouse)
	{
		isMouseMove = true;
		CameraManip.mouseMove(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam), inputs);
		CameraManip.sunMove(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam));
	}
		

}

void RestirGI::OnKeyUp(UINT8 key)
{
	if (key == VK_SPACE) {
		m_raster = !m_raster;
	}
	m_keys[key] = false;
	isKeyboardMove = false;
}

DXGI_FORMAT RestirGI::getRTVFormat(std::string rtv)
{
	if (rtv == "normal")
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	if (rtv == "velocity")
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	return DXGI_FORMAT_R32G32B32A32_FLOAT;
}

void RestirGI::OnKeyDown(UINT8 key)
{
	isKeyboardMove = true;
	m_keys[key] = true;
}

void RestirGI::Initialize()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer
	{
		ComPtr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
#ifdef DEBUGSHADER
			debugController->SetEnableGPUBasedValidation(true);
#endif // DEBUGSHADER
			debugController->SetEnableSynchronizedCommandQueueValidation(true);

			ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue))))
			{
				dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
				dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
			}

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
}
#endif

	//create device
	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice) {
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
		ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
	}
	else {
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);
		ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));
	}
	if (dxgiFactoryFlags & DXGI_CREATE_FACTORY_DEBUG)
	{
		ComPtr<ID3D12InfoQueue> d3dInfoQueue;
		if (SUCCEEDED(m_device.As(&d3dInfoQueue)))
		{
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		}
	}

	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Describe and create the command queue.
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
		ThrowIfFailed(m_device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(m_commandList.GetAddressOf())));
		//m_commandList->Close();
	}

	// Describe and create the swap chain.
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.BufferCount = FrameCount;
		swapChainDesc.Width = m_width;
		swapChainDesc.Height = m_height;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.SampleDesc.Count = 1;

		ComPtr<IDXGISwapChain1> swapChain;
		ThrowIfFailed(factory->CreateSwapChainForHwnd(
			m_commandQueue.Get(),
			Win32Application::GetHwnd(), &swapChainDesc, nullptr, nullptr, &swapChain));
		// This sample does not support fullscreen transitions.
		ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));
		ThrowIfFailed(swapChain.As(&m_swapChain));
	}
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create rtv/dsv descriptor heaps .
	{
		//rtv:add gbuffer rt to rtvheap
		m_rtvHeap = helper::CreateDescriptorHeap(m_device.Get(), FrameCount + MRTCount, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++) {
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}


		//dsv:add gbuffer ds to dsvheap
		m_dsvHeap = helper::CreateDescriptorHeap(m_device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);
		D3D12_HEAP_PROPERTIES depthHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_RESOURCE_DESC depthResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			m_dsvFormat, m_width, m_height, 1, 1);
		depthResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
#ifdef REVERSE_Z
		CD3DX12_CLEAR_VALUE depthOptimizedClearValue(m_dsvFormat, 0.0f, 0);
#else
		CD3DX12_CLEAR_VALUE depthOptimizedClearValue(m_dsvFormat, 1.0f, 0);
#endif

		// Allocate the buffer itself, with a state allowing depth writes
		ThrowIfFailed(m_device->CreateCommittedResource(
			&depthHeapProperties, D3D12_HEAP_FLAG_NONE, &depthResourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue,
			IID_PPV_ARGS(&m_dsBuffer)));

		// Write the depth buffer view into the depth buffer heap
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = m_dsvFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;


		m_device->CreateDepthStencilView(m_dsBuffer.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	//ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
		// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr) {
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}
	}
}

void RestirGI::LoadAssets()
{
	//Screen Quad
	{
		std::unique_ptr<Mesh> lightPass = std::make_unique<Mesh>();
		lightPass->Name = "lightPass";

		Vertex_Light QuadVerts[] =
		{
			{ { -1.0f,1.0f, 0.0f},{ 0.0f,0.0f } },
			{ { 1.0f, 1.0f, 0.0f}, {1.0f,0.0f } },
			{ { -1.0f, -1.0f, 0.0f},{ 0.0f,1.0f } },
			{ { 1.0f, -1.0f, 0.0f},{ 1.0f,1.0f } }
		};

		const UINT vertexBufferSize = sizeof(QuadVerts);
		lightPass->VertexBufferByteSize = vertexBufferSize;
		lightPass->VertexByteStride = sizeof(Vertex_Light);
		lightPass->VertexCount = 4.;
		lightPass->VertexBufferGPU = helper::CreateDefaultBuffer(m_device.Get(), m_commandList.Get(),
			QuadVerts, vertexBufferSize, lightPass->VertexBufferUploader);

		m_meshes["lightPass"] = std::move(lightPass);
	}

	//load model
	m_textloader.Initialize(m_device.Get(), m_commandList.Get());
	{
		ModelLoader modelLoader(m_device.Get(), m_commandList.Get(), &m_textloader);
		modelLoader.Load("Resource/Model/sponza/sponza.obj", m_sceneModel);//CornellBox/CornellBox-Original.obj sponza/sponza.obj
	}

	//load dds
	m_ddsloader.Initialize(m_device.Get(), m_commandList.Get());
	{
		std::shared_ptr<Texture> texture = std::make_shared<Texture>();
		m_ddsloader.Load("Resource/bluenoise/256_256/LDR_RGBA_0.png", texture);

	}
}

void RestirGI::WaitForPreviousFrame()
{
	//This is code implemented as such for simplicity. use FRAME RESOURCE can be more efficient
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence) {
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void RestirGI::CreateGBufferResource()
{


	auto createRTBufferAndRTVSRV = [this](UINT rtBufferOffset, UINT rtvHandleOffset, UINT srvHandleOffset, bool _createRtv = true, DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT) {
		CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);
		//create buffer
		D3D12_RESOURCE_DESC resourceDesc = {};
		ZeroMemory(&resourceDesc, sizeof(resourceDesc));
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
		resourceDesc.Format = format;

		float clearColor[4]{ 0.0,0.0,0.0,1.0f };
		CD3DX12_CLEAR_VALUE clearValue(format, clearColor);
		ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(m_rtBuffer[rtBufferOffset].GetAddressOf())));

		if (_createRtv)
		{
			//create rtv
			D3D12_RENDER_TARGET_VIEW_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.Texture2D.MipSlice = 0;
			desc.Texture2D.PlaneSlice = 0;
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			desc.Format = format;

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
			rtvHandle.Offset(rtvHandleOffset, m_rtvDescriptorSize);
			m_device->CreateRenderTargetView(m_rtBuffer[rtBufferOffset].Get(), &desc, rtvHandle);
		}

		//create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV;
		ZeroMemory(&descSRV, sizeof(descSRV));
		descSRV.Texture2D.MipLevels = 1;
		descSRV.Texture2D.MostDetailedMip = 0;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		descSRV.Format = format;

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(srvHandleOffset, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_rtBuffer[rtBufferOffset].Get(), &descSRV, srvHandle);


	};


	//create gbuffer for RasterGbufferPass.hlsl

	createRTBufferAndRTVSRV(0, 2 + 0, 10 + 0);	//0:gbuffer
	createRTBufferAndRTVSRV(1, 2 + 1, 10 + 1, true, getRTVFormat("normal"));	//1:normal
	createRTBufferAndRTVSRV(2, 2 + 2, 10 + 2);	//2:world positon
	createRTBufferAndRTVSRV(3, 2 + 3, 10 + 3, true, getRTVFormat("velocity"));	//3:move vector
	//RTV目标只需要创建srv
	TexMap.insert({ "gbuffer",HandleOffset(0,10 + 0,0) });
	TexMap.insert({ "normal",HandleOffset(1,10 + 1,0) });
	TexMap.insert({ "worldPositon",HandleOffset(2,10 + 2,0) });
	TexMap.insert({ "velocity",HandleOffset(3,10 + 3,0) });
	m_rtBuffer[0]->SetName(L"gbuffer");
	m_rtBuffer[1]->SetName(L"normal");
	m_rtBuffer[2]->SetName(L"worldPositon");
	m_rtBuffer[3]->SetName(L"velocity");


	//create ds buffer's  srv
	{
		//create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV;
		ZeroMemory(&descSRV, sizeof(descSRV));
		descSRV.Texture2D.MipLevels = 1;
		descSRV.Texture2D.MostDetailedMip = 0;
#ifdef REVERSE_Z
		descSRV.Format = DXGI_FORMAT_R32_FLOAT;
#else
		descSRV.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
#endif // REVERSE_Z

		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(10 + RasterGBufferCount, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_dsBuffer.Get(), &descSRV, srvHandle);

		//depth只需要传入正确的srv即可
		TexMap.insert({ "depth",HandleOffset(0,10 + RasterGBufferCount,0) });
	}

	//create srv for raytraing outputbuffer
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(20, m_cbvSrvUavDescriptorSize);

		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV;
		ZeroMemory(&descSRV, sizeof(descSRV));
		descSRV.Texture2D.MipLevels = 1;
		descSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		descSRV.Texture2D.MostDetailedMip = 0;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		m_device->CreateShaderResourceView(m_outputResource.Get(), &descSRV, srvHandle);
		//这里rtBuffer的值不重要，传入0
		TexMap.insert({ "output",HandleOffset(0,20,0) });

	}

	//create gbuffer for TemporalFilter.hlsl
	//4: temporal texture
	{
		createRTBufferAndRTVSRV(4, 2 + 4, 50);
	}

	//create history buffer
	{
		createRTBufferAndRTVSRV(15, -1, 25, false); //history color
	}


	//create gbuffer(output)  for ATrousFilter.hlsl	
	{
		//gbuffer :7-8 atrous texture0 and 1  //rtvhandle: 9-10
		createRTBufferAndRTVSRV(7, 2 + 7, 60);
		createRTBufferAndRTVSRV(8, 2 + 8, 61);
	}

	//create Irradiance




	//创建纹理模板
	auto CreateTexture = [this](std::string name, UINT width, UINT height, DXGI_FORMAT format) {
		CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);

		//create buffer
		D3D12_RESOURCE_DESC resourceDesc = {};
		ZeroMemory(&resourceDesc, sizeof(resourceDesc));
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.MipLevels = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resourceDesc.Format = format;


		ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(m_rtBuffer[resourceBufferOffset].GetAddressOf())));


		//std::wstring Name = name;
		m_rtBuffer[resourceBufferOffset]->SetName(stringToLPCWSTR(name));

		//create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		ZeroMemory(&descSRV, sizeof(descSRV));
		descSRV.Texture2D.MipLevels = 1;
		descSRV.Texture2D.MostDetailedMip = 0;
		descSRV.Format = format;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(srvAnduavOffset, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_rtBuffer[resourceBufferOffset].Get(), &descSRV, srvHandle);

		//create uav
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		uavHandle.Offset(srvAnduavOffset + 1, m_cbvSrvUavDescriptorSize);

		m_device->CreateUnorderedAccessView(m_rtBuffer[resourceBufferOffset].Get(), nullptr, &uavDesc, uavHandle);

		TexMap.insert({ name,HandleOffset(resourceBufferOffset,srvAnduavOffset,srvAnduavOffset + 1) });
		resourceBufferOffset++;
		srvAnduavOffset = srvAnduavOffset + 2;

	};



	//创建Buffer模板
	auto createBufferAndRWBuffer = [this](std::string name, UINT numSize, UINT numelements, bool isStructured = true)
	{
		CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);
		//create buffer
		D3D12_RESOURCE_DESC resourceDesc = {};
		ZeroMemory(&resourceDesc, sizeof(resourceDesc));
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = 0;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.MipLevels = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.Width = static_cast<UINT64>(numSize) * numelements;
		resourceDesc.Height = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;

		ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(m_rtBuffer[resourceBufferOffset].GetAddressOf())));

		m_rtBuffer[resourceBufferOffset]->SetName(stringToLPCWSTR(name));

		//create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		ZeroMemory(&descSRV, sizeof(descSRV));
		if (isStructured)
		{
			descSRV.Format = DXGI_FORMAT_UNKNOWN;
			descSRV.Buffer.FirstElement = 0;
			descSRV.Buffer.NumElements = numelements;
			descSRV.Buffer.StructureByteStride = numSize;
			descSRV.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		}
		else
		{
			descSRV.Format = DXGI_FORMAT_R32_TYPELESS;
			descSRV.Buffer.FirstElement = 0;
			descSRV.Buffer.NumElements = numelements * (numSize / sizeof(uint));
			descSRV.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		}

		descSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(srvAnduavOffset, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_rtBuffer[resourceBufferOffset].Get(), &descSRV, srvHandle);

		//create uav
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

		if (isStructured)
		{
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = numelements;
			uavDesc.Buffer.StructureByteStride = numSize;
		}
		else
		{
			uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = numelements * (numSize / sizeof(uint));
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
		}


		//uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		uavHandle.Offset(srvAnduavOffset + 1, m_cbvSrvUavDescriptorSize);

		m_device->CreateUnorderedAccessView(m_rtBuffer[resourceBufferOffset].Get(), nullptr, &uavDesc, uavHandle);

		TexMap.insert({ name,HandleOffset(resourceBufferOffset,srvAnduavOffset,srvAnduavOffset + 1) });
		resourceBufferOffset++;
		srvAnduavOffset = srvAnduavOffset + 2;
	};

	CreateTexture("test", m_width, m_height, DXGI_FORMAT_R32G32B32A32_FLOAT);
	CreateTexture("test2", m_width, m_height, DXGI_FORMAT_R32G32B32A32_FLOAT);
	//Restir
	CreateTexture("prevDepth", m_width, m_height, DXGI_FORMAT_R32_FLOAT);
	CreateTexture("reprojection_map", m_width, m_height, DXGI_FORMAT_R16G16B16A16_SNORM);
	CreateTexture("half_view_normal_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R8G8B8A8_SNORM);
	CreateTexture("half_depth_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R32_FLOAT);
	CreateTexture("accumImg", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT); //pre irradiance
	CreateTexture("in_ssgiTex", m_width / 2, m_height / 2, DXGI_FORMAT_R16_FLOAT);
	CreateTexture("spatiallyFilteredTex", m_width / 2, m_height / 2, DXGI_FORMAT_R16_FLOAT);
	CreateTexture("upsampledTex", m_width, m_height, DXGI_FORMAT_R16_FLOAT);
	CreateTexture("ssgi_tex", m_width, m_height, DXGI_FORMAT_R8_UNORM);
	CreateTexture("ssaoTemporal_historyTex", m_width, m_height, DXGI_FORMAT_R16_FLOAT); //swap
	CreateTexture("ssaoTemporal_historyOutputTex", m_width, m_height, DXGI_FORMAT_R16_FLOAT);
	CreateTexture("wrc", 1, 1, DXGI_FORMAT_R8_UNORM);
	CreateTexture("sunShadowMask", m_width, m_height, DXGI_FORMAT_R8_UNORM);
	CreateTexture("bitpacked_shadows_image", m_width/8, m_height/4, DXGI_FORMAT_R32_UINT);
	CreateTexture("prev_moments_image", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT); //swap
	CreateTexture("moments_image", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("prev_accum_image", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT);//swap
	CreateTexture("accum_image", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT);
	CreateTexture("denoised_shadow_mask", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT);
	CreateTexture("temp", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT);
	CreateTexture("metadata_image", m_width / 8, m_height / 4, DXGI_FORMAT_R32_UINT);
	CreateTexture("temporal_history_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT); //swap
	CreateTexture("temporal_output_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("reprojected_history_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("half_ssao_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R8_UNORM);
	CreateTexture("ray_history_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT); //swap
	CreateTexture("ray_output_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("ray_orig_history_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R32G32B32A32_FLOAT); //swap
	CreateTexture("ray_orig_output_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R32G32B32A32_FLOAT);
	CreateTexture("reservoir_history_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R32G32_UINT); //swap
	CreateTexture("reservoir_output_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R32G32_UINT);
	CreateTexture("radiance_history_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT); //swap
	CreateTexture("radiance_output_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("rt_history_validity_pre_input_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R8_UNORM);
	CreateTexture("rt_history_validity_input_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R8_UNORM);
	CreateTexture("candidate_radiance_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("candidate_normal_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R8G8B8A8_SNORM);
	CreateTexture("candidate_hit_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("invalidity_history_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16_FLOAT);
	CreateTexture("invalidity_output_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16_FLOAT);
	CreateTexture("hit_normal_history_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("hit_normal_output_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("candidate_history_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("candidate_output_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("temporal_reservoir_packed_tex", m_width / 2, m_height / 2, DXGI_FORMAT_R32G32B32A32_UINT);
	CreateTexture("reservoir_output_tex0", m_width / 2, m_height / 2, DXGI_FORMAT_R32G32_UINT);
	CreateTexture("reservoir_output_tex1", m_width / 2, m_height / 2, DXGI_FORMAT_R32G32_UINT);
	CreateTexture("bounced_radiance_output_tex0", m_width / 2, m_height / 2, DXGI_FORMAT_R11G11B10_FLOAT);
	CreateTexture("bounced_radiance_output_tex1", m_width / 2, m_height / 2, DXGI_FORMAT_R11G11B10_FLOAT);
	CreateTexture("irradiance_output_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("variance_history_tex", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT); //swap
	CreateTexture("temporal_variance_output_tex", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT);
	CreateTexture("temporal_filtered_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("spatial_filtered_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("debug_out_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("path_trace_tex", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);

	CreateTexture("RMSE", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	
	//Lumen 
	CreateTexture("probe_radiance", probeTexSize.x * SCREEN_PROBE_RESOLUTION, probeTexSize.y * SCREEN_PROBE_RESOLUTION, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("screen_probe_scene_depth", probeTexSize.x, probeTexSize.y, DXGI_FORMAT_R32_FLOAT);
	CreateTexture("screen_probe_scene_history_depth", probeTexSize.x, probeTexSize.y, DXGI_FORMAT_R32_FLOAT);
	CreateTexture("screen_tile_adaptive_probe_header", ceil(m_width / PROBE_RESOLUTION), ceil(m_height / PROBE_RESOLUTION), DXGI_FORMAT_R32_UINT);
	CreateTexture("screen_tile_adaptive_probe_indices", m_width, m_height, DXGI_FORMAT_R16_UINT);
	CreateTexture("probe_hit_distance", probeTexSize.x* SCREEN_PROBE_RESOLUTION, probeTexSize.y* SCREEN_PROBE_RESOLUTION, DXGI_FORMAT_R16_FLOAT);
	CreateTexture("probe_reservoir", probeTexSize.x* SCREEN_PROBE_RESOLUTION, probeTexSize.y* SCREEN_PROBE_RESOLUTION, DXGI_FORMAT_R32G32_UINT);
	CreateTexture("probe_vertex_packed", probeTexSize.x* SCREEN_PROBE_RESOLUTION, probeTexSize.y* SCREEN_PROBE_RESOLUTION, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("probe_pre_direction", probeTexSize.x* SCREEN_PROBE_RESOLUTION, probeTexSize.y* SCREEN_PROBE_RESOLUTION, DXGI_FORMAT_R16G16B16A16_FLOAT);
	CreateTexture("rt_history_validity_tex", probeTexSize.x* SCREEN_PROBE_RESOLUTION, probeTexSize.y* SCREEN_PROBE_RESOLUTION, DXGI_FORMAT_R8_UNORM);
	CreateTexture("variance_history_tex_Lumen", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT); //swap
	CreateTexture("temporal_variance_output_tex_Lumen", m_width, m_height, DXGI_FORMAT_R16G16_FLOAT);
	CreateTexture("temporal_filtered_tex_Lumen", m_width, m_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	

	createBufferAndRWBuffer("adaptive_screen_probe_data", sizeof(uint), probeTexSize.x* probeTexSize.y - ceil(m_width / PROBE_RESOLUTION) * ceil(m_height / PROBE_RESOLUTION));
	createBufferAndRWBuffer("num_adaptive_screen_probes", sizeof(uint), 1);
	createBufferAndRWBuffer("probe_SH", sizeof(float)*4 , probeTexSize.x * probeTexSize.y * 3);//3 * 最大探针数
	createBufferAndRWBuffer("my_probe_SH", sizeof(float) * 3, probeTexSize.x* probeTexSize.y * 4);//3 * 最大探针数
	
	
	//CreateTexture("")
	createBufferAndRWBuffer("ircache_meta_buf", sizeof(uint), 8, false);
	createBufferAndRWBuffer("ircache_grid_meta_buf", sizeof(uint) * 2, MAX_GRID_CELLS, false);//要手动指定纹理handle
	createBufferAndRWBuffer("ircache_grid_meta_buf2", sizeof(uint) * 2, MAX_GRID_CELLS, false);//要手动指定纹理handle
	createBufferAndRWBuffer("ircache_entry_cell_buf", sizeof(uint), MAX_ENTRIES);
	createBufferAndRWBuffer("ircache_spatial_buf", sizeof(float) * 4, MAX_ENTRIES);
	createBufferAndRWBuffer("ircache_irradiance_buf", sizeof(float) * 4, MAX_ENTRIES * 3);
	createBufferAndRWBuffer("ircache_aux_buf", sizeof(float) * 4, MAX_ENTRIES * 4 * 16);
	createBufferAndRWBuffer("ircache_life_buf", sizeof(uint), MAX_ENTRIES);
	createBufferAndRWBuffer("ircache_pool_buf", sizeof(uint), MAX_ENTRIES);
	createBufferAndRWBuffer("ircache_entry_indirection_buf", sizeof(uint), INDIRECTION_BUF_ELEM_COUNT);
	createBufferAndRWBuffer("ircache_reposition_proposal_buf", sizeof(float) * 4, MAX_ENTRIES);
	createBufferAndRWBuffer("ircache_reposition_proposal_count_buf", sizeof(uint), MAX_ENTRIES);

	createBufferAndRWBuffer("_ircacheDispatchArgs", sizeof(uint), 4 * 2, false);
	createBufferAndRWBuffer("entry_occupancy_buf", sizeof(uint), MAX_ENTRIES);
	createBufferAndRWBuffer("segment_sum_buf", sizeof(uint), 1024, false); //SEGMENT_SIZE = 1024
	createBufferAndRWBuffer("_ircacheDispatchArgs2", sizeof(uint), 4 * 4, false);

	createBufferAndRWBuffer("testBuffer", sizeof(uint), 8, false);
	createBufferAndRWBuffer("RMSE_buf", sizeof(float), 4);

	//create MeshMaterialBuffer srv
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		ZeroMemory(&descSRV, sizeof(descSRV));

		descSRV.Format = DXGI_FORMAT_UNKNOWN;
		descSRV.Buffer.FirstElement = 0;
		descSRV.Buffer.NumElements = m_sceneModel.Mat.size();
		descSRV.Buffer.StructureByteStride = sizeof(MeshMaterial);
		descSRV.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(19, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_sceneModel.MaterialBuffer.Get(), &descSRV, srvHandle);
		//只需创建srv
		TexMap.insert({ "MeshMaterialBuffer",HandleOffset(0,19,0) });
	}
	//create brdf_fg
	{
		CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);

		DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		//create buffer
		D3D12_RESOURCE_DESC resourceDesc = {};
		ZeroMemory(&resourceDesc, sizeof(resourceDesc));
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.MipLevels = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.Width = 64;
		resourceDesc.Height = 64;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resourceDesc.Format = format;


		ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(m_rtBuffer[resourceBufferOffset].GetAddressOf())));


		//std::wstring Name = name;
		m_rtBuffer[resourceBufferOffset]->SetName(L"brdf_fg");

		//create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		ZeroMemory(&descSRV, sizeof(descSRV));
		descSRV.Texture2D.MipLevels = 1;
		descSRV.Texture2D.MostDetailedMip = 0;
		descSRV.Format = format;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(MESH_TEXTURE_STARTINDEX + 198, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_rtBuffer[resourceBufferOffset].Get(), &descSRV, srvHandle);

		//create uav
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		uavHandle.Offset(srvAnduavOffset, m_cbvSrvUavDescriptorSize);

		m_device->CreateUnorderedAccessView(m_rtBuffer[resourceBufferOffset].Get(), nullptr, &uavDesc, uavHandle);

		TexMap.insert({ "brdf_fg",HandleOffset(resourceBufferOffset,MESH_TEXTURE_STARTINDEX + 198,srvAnduavOffset) });
		resourceBufferOffset++;
		srvAnduavOffset = srvAnduavOffset + 1;
	}
	//create default texture(albedo/speclur)
	{
		//DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;

		//CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);
		//CD3DX12_HEAP_PROPERTIES uploadProperty(D3D12_HEAP_TYPE_UPLOAD);
		////create buffer
		//D3D12_RESOURCE_DESC resourceDesc = {};
		//ZeroMemory(&resourceDesc, sizeof(resourceDesc));
		//resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		//resourceDesc.Alignment = 0;
		//resourceDesc.SampleDesc.Count = 1;
		//resourceDesc.SampleDesc.Quality = 0;
		//resourceDesc.MipLevels = 1;
		//resourceDesc.DepthOrArraySize = 1;
		//resourceDesc.Width = 2;
		//resourceDesc.Height = 2;
		//resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		//resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		//resourceDesc.Format = format;

		//

		//ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		//	D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(m_defaultTexture.GetAddressOf())));

		//D3D12_RESOURCE_DESC bufDesc = {};
		//bufDesc.Alignment = 0;
		//bufDesc.DepthOrArraySize = 1;
		//bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		//bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		//bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		//bufDesc.Height = 2;
		//bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		//bufDesc.MipLevels = 1;
		//bufDesc.SampleDesc.Count = 1;
		//bufDesc.SampleDesc.Quality = 0;
		//bufDesc.Width = 2;

		//ThrowIfFailed(m_device->CreateCommittedResource(&uploadProperty, D3D12_HEAP_FLAG_NONE, &bufDesc,
		//	D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_uploadTexture.GetAddressOf())));

		//std::vector<std::vector<float4>> textureMap(2,std::vector<float4>(2, float4(1.0, 1.0, 1.0, 1.0)));
		//textureMap[0][1] = float4(0.1, 0.2, 0.3, 0.4);
		//textureMap[1][0] = float4(0.2, 0.4, 0.6, 0.8);
		//textureMap[1][1] = float4(0.1, 0.3, 0.5, 0.7);
		//

		//D3D12_SUBRESOURCE_DATA subResourceData = {};
		//subResourceData.pData = textureMap.data();
		//subResourceData.RowPitch = 2 * sizeof(float4);
		//subResourceData.SlicePitch = 2 * 2 * sizeof(float4);

		//CD3DX12_RESOURCE_BARRIER commonToCopy = CD3DX12_RESOURCE_BARRIER::Transition(m_defaultTexture.Get(),
		//	D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		//m_commandList->ResourceBarrier(1, &commonToCopy);

		//UpdateSubresources<1>(m_commandList.Get(), m_defaultTexture.Get(), m_uploadTexture.Get(), 0, 0, 1, &subResourceData);
		////mCommandList->CopyBufferRegion(VertexBufferGPU.Get(), 0, VertexBufferUploader.Get(), 0, vertex_total_bytes);
		////m_commandList->CopyTextureRegion();

		//CD3DX12_RESOURCE_BARRIER copyToRead = CD3DX12_RESOURCE_BARRIER::Transition(m_defaultTexture.Get(),
		//	D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		//m_commandList->ResourceBarrier(1, &copyToRead);

		////create srv
		//D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		//ZeroMemory(&descSRV, sizeof(descSRV));
		//descSRV.Texture2D.MipLevels = 1;
		//descSRV.Texture2D.MostDetailedMip = 0;
		//descSRV.Format = format;
		//descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		//descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		//CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		//srvHandle.Offset(25, m_cbvSrvUavDescriptorSize);
		//m_device->CreateShaderResourceView(m_rtBuffer[resourceBufferOffset].Get(), &descSRV, srvHandle);

		////只需创建srv
		//TexMap.insert({ "DefaultTexture",HandleOffset(0,25,0) });
	}
	//create sky box
	{
		CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);

		DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		//create buffer
		D3D12_RESOURCE_DESC resourceDesc = {};
		ZeroMemory(&resourceDesc, sizeof(resourceDesc));
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.MipLevels = 1;
		resourceDesc.DepthOrArraySize = 6; //
		resourceDesc.Width = 64;
		resourceDesc.Height = 64;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resourceDesc.Format = format;

		ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(m_rtBuffer[resourceBufferOffset].GetAddressOf())));

		m_rtBuffer[resourceBufferOffset]->SetName(L"sky_cube");

		//create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		ZeroMemory(&descSRV, sizeof(descSRV));
		descSRV.TextureCube.MostDetailedMip = 0;
		descSRV.TextureCube.MipLevels = 1;
		descSRV.TextureCube.ResourceMinLODClamp = 0;
		descSRV.Format = format;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(srvAnduavOffset, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_rtBuffer[resourceBufferOffset].Get(), &descSRV, srvHandle);

		//create uav
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.PlaneSlice = 0;
		uavDesc.Texture2DArray.ArraySize = 6;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		uavHandle.Offset(srvAnduavOffset + 1, m_cbvSrvUavDescriptorSize);

		m_device->CreateUnorderedAccessView(m_rtBuffer[resourceBufferOffset].Get(), nullptr, &uavDesc, uavHandle);

		TexMap.insert({ "sky_cube",HandleOffset(resourceBufferOffset,srvAnduavOffset,srvAnduavOffset + 1) });
		resourceBufferOffset++;
		srvAnduavOffset = srvAnduavOffset + 2;
	}
	//create convolue sky
	{
		CD3DX12_HEAP_PROPERTIES heapProperty(D3D12_HEAP_TYPE_DEFAULT);

		DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		//create buffer
		D3D12_RESOURCE_DESC resourceDesc = {};
		ZeroMemory(&resourceDesc, sizeof(resourceDesc));
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.MipLevels = 1;
		resourceDesc.DepthOrArraySize = 6; //
		resourceDesc.Width = 16;
		resourceDesc.Height = 16;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resourceDesc.Format = format;

		ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &resourceDesc,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(m_rtBuffer[resourceBufferOffset].GetAddressOf())));

		m_rtBuffer[resourceBufferOffset]->SetName(L"convolve_sky");

		//create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		ZeroMemory(&descSRV, sizeof(descSRV));
		descSRV.TextureCube.MostDetailedMip = 0;
		descSRV.TextureCube.MipLevels = 1;
		descSRV.TextureCube.ResourceMinLODClamp = 0;
		descSRV.Format = format;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(srvAnduavOffset, m_cbvSrvUavDescriptorSize);
		m_device->CreateShaderResourceView(m_rtBuffer[resourceBufferOffset].Get(), &descSRV, srvHandle);

		//create uav
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.PlaneSlice = 0;
		uavDesc.Texture2DArray.ArraySize = 6;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		uavHandle.Offset(srvAnduavOffset + 1, m_cbvSrvUavDescriptorSize);

		m_device->CreateUnorderedAccessView(m_rtBuffer[resourceBufferOffset].Get(), nullptr, &uavDesc, uavHandle);

		TexMap.insert({ "convolve_sky",HandleOffset(resourceBufferOffset,srvAnduavOffset,srvAnduavOffset + 1) });
		resourceBufferOffset++;
		srvAnduavOffset = srvAnduavOffset + 2;
	}
}

void RestirGI::CreateCommonRasterPipeline()
{

	{
		auto staticSamplers = GetStaticSamplers();
		std::vector<int> srv, uav;
		uint cbvNums = 0;

		//默认创建模板
		auto CreateSignature = [this, staticSamplers](std::vector<int>& srv, std::vector<int>& uav, uint cbvNums, ComPtr<ID3D12RootSignature>& pRootSignature, bool useSamples = false) {
			nv_helpers_dx12::RootSignatureGenerator RSG;

			std::vector<std::tuple<UINT, /* BaseShaderRegister, */ UINT, /* NumDescriptors */ UINT,
				/* RegisterSpace */ D3D12_DESCRIPTOR_RANGE_TYPE,
				/* RangeType */ UINT /* OffsetInDescriptorsFromTableStart */>>
				ranges;
			//srv
			if (srv.size() != 0)
			{
				for (uint i = 0; i < srv.size(); ++i)
				{
					ranges.push_back({ i,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV ,srv[i] });
				}
				RSG.AddHeapRangesParameter(ranges);
				ranges.clear();
			}

			//uav
			if (uav.size() != 0)
			{
				for (uint i = 0; i < uav.size(); ++i)
				{
					ranges.push_back({ i,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV ,uav[i] });
				}
				RSG.AddHeapRangesParameter(ranges);
				ranges.clear();
			}

			for (uint i = 0; i < cbvNums; ++i)
			{
				RSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, i);
			}

			if (!useSamples)
				pRootSignature = RSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
			else
				pRootSignature = RSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, (UINT)staticSamplers.size(), staticSamplers.data());
		};

		// Create common raster root signature.

		nv_helpers_dx12::RootSignatureGenerator rasterCommonRSG;
		rasterCommonRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
		rasterCommonRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 1);
		rasterCommonRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });
		rasterCommonRSG.AddHeapRangesParameter({ {1,4,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });
		m_rasterRootSignature = rasterCommonRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			(UINT)staticSamplers.size(), staticSamplers.data());

		//Create temporal filter pass root signature
		nv_helpers_dx12::RootSignatureGenerator rasterTemporalFilterRSG;
		rasterTemporalFilterRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
		rasterTemporalFilterRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//raytracing output
		m_rasterTemporalFilterSignature = rasterTemporalFilterRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		//Create svgf Atrous filter pass root signature
		nv_helpers_dx12::RootSignatureGenerator rasterAtrousFilterRSG;
		rasterAtrousFilterRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);//atrous constant buffer
		rasterAtrousFilterRSG.AddHeapRangesParameter({
				{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,10}, //gbuffer
				{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,11}, //normal
				{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,12}, //world positon

			});
		rasterAtrousFilterRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//temporal texuture or astrousRead texture or variance texture
		m_rasterAtrousFilterSignature = rasterAtrousFilterRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);


		//Create post processing pass root signature
		nv_helpers_dx12::RootSignatureGenerator rasterPostProcessingRSG;
		rasterPostProcessingRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
		rasterPostProcessingRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//atrous texture or temporal texture
		rasterPostProcessingRSG.AddHeapRangesParameter({
				{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,10},//first pass gbuffer
				{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,11},//first pass normal
				{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,12},//first pass position
				{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,14},//first pass depth
			});
		m_rasterPostProcessingSignature = rasterPostProcessingRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);




	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(L"shaders/shaders.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders/shaders.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rasterRootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
#ifdef REVERSE_Z
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
#endif
		psoDesc.DSVFormat = m_dsvFormat;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_rasterPiplineState)));

		//create gbuffer first pass
		ComPtr<ID3DBlob> gbufferPassVertexShader;
		ComPtr<ID3DBlob> gbufferPasspixelShader;
		ThrowIfFailed(D3DCompileFromFile(L"shaders/RasterGbufferPass.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", compileFlags, 0, &gbufferPassVertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders/RasterGbufferPass.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", compileFlags, 0, &gbufferPasspixelShader, nullptr));

		psoDesc.pRootSignature = m_rasterRootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(gbufferPassVertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(gbufferPasspixelShader.Get());
		psoDesc.NumRenderTargets = RasterGBufferCount;
		psoDesc.RTVFormats[0] = m_rtvFormat;
		psoDesc.RTVFormats[1] = getRTVFormat("normal");
		psoDesc.RTVFormats[2] = m_rtvFormat;
		psoDesc.RTVFormats[3] = getRTVFormat("velocity");
		psoDesc.DSVFormat = m_dsvFormat;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_rasterGBufferPSO)));

		//create light pass
		ComPtr<ID3DBlob> lightPassVertexShader;
		ComPtr<ID3DBlob> lightPassPixelShader;
		ThrowIfFailed(D3DCompileFromFile(L"shaders/RasterLightPass.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", compileFlags, 0, &lightPassVertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders/RasterLightPass.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", compileFlags, 0, &lightPassPixelShader, nullptr));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC lightpsoDesc = {};
		lightpsoDesc.InputLayout = { inputLightElementDescs, _countof(inputLightElementDescs) };
		lightpsoDesc.pRootSignature = m_rasterRootSignature.Get();
		lightpsoDesc.VS = CD3DX12_SHADER_BYTECODE(lightPassVertexShader.Get());
		lightpsoDesc.PS = CD3DX12_SHADER_BYTECODE(lightPassPixelShader.Get());
		lightpsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		lightpsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		lightpsoDesc.RasterizerState.DepthClipEnable = false;
		lightpsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		lightpsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		lightpsoDesc.DepthStencilState.DepthEnable = false;//in light pass we dont need output depthstencil
		lightpsoDesc.DSVFormat = m_dsvFormat;
		lightpsoDesc.SampleMask = UINT_MAX;
		lightpsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		lightpsoDesc.NumRenderTargets = 1;
		lightpsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		lightpsoDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&lightpsoDesc, IID_PPV_ARGS(&m_rasterLightPSO)));

		//--------------------------------------------
		//temporal filter pass
		//---------------------------------------------
		ComPtr<ID3DBlob> temporalFilterVertexShader;
		ComPtr<ID3DBlob> temporalFilterPixelShader;

		ThrowIfFailed(D3DCompileFromFile(L"shaders/PostProcessingVertex.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", compileFlags, 0, &temporalFilterVertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders/TemporalFilterPixel.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", compileFlags, 0, &temporalFilterPixelShader, nullptr));

		// temporal filter pass pso
		D3D12_GRAPHICS_PIPELINE_STATE_DESC temporalFilterPSO = {};
		temporalFilterPSO.InputLayout = { inputLightElementDescs, _countof(inputLightElementDescs) };
		temporalFilterPSO.pRootSignature = m_rasterTemporalFilterSignature.Get();
		temporalFilterPSO.VS = CD3DX12_SHADER_BYTECODE(temporalFilterVertexShader.Get());
		temporalFilterPSO.PS = CD3DX12_SHADER_BYTECODE(temporalFilterPixelShader.Get());
		temporalFilterPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		temporalFilterPSO.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		temporalFilterPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		temporalFilterPSO.SampleMask = UINT_MAX;
		temporalFilterPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		temporalFilterPSO.NumRenderTargets = 1;
		temporalFilterPSO.RTVFormats[0] = m_rtvFormat;
		temporalFilterPSO.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&temporalFilterPSO, IID_PPV_ARGS(&m_rasterTemporalFilterPSO)));

		//--------------------------------------------
		//atrous filter pass
		//---------------------------------------------
		ComPtr<ID3DBlob> atrousFilterVertexShader;
		ComPtr<ID3DBlob> atrousFilterPixelShader;

		ThrowIfFailed(D3DCompileFromFile(L"shaders/PostProcessingVertex.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", compileFlags, 0, &atrousFilterVertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders/SVGFATrousFilterPixel.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", compileFlags, 0, &atrousFilterPixelShader, nullptr));
		// Atrous filter pass pso
		D3D12_GRAPHICS_PIPELINE_STATE_DESC atrousFilterPSO = {};
		atrousFilterPSO.InputLayout = { inputLightElementDescs, _countof(inputLightElementDescs) };
		atrousFilterPSO.pRootSignature = m_rasterAtrousFilterSignature.Get();
		atrousFilterPSO.VS = CD3DX12_SHADER_BYTECODE(atrousFilterVertexShader.Get());
		atrousFilterPSO.PS = CD3DX12_SHADER_BYTECODE(atrousFilterPixelShader.Get());
		atrousFilterPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		atrousFilterPSO.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		atrousFilterPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		atrousFilterPSO.SampleMask = UINT_MAX;
		atrousFilterPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		atrousFilterPSO.NumRenderTargets = 1;
		atrousFilterPSO.RTVFormats[0] = m_rtvFormat;//DXGI_FORMAT_R8G8B8A8_UNORM
		atrousFilterPSO.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&atrousFilterPSO, IID_PPV_ARGS(&m_rasterAtrousFilterPSO)));


		//--------------------------------------------
		//post processing pass
		//---------------------------------------------
		ComPtr<ID3DBlob> postProcessingVertexShader;
		ComPtr<ID3DBlob> postProcessingPixelShader;

		ThrowIfFailed(D3DCompileFromFile(L"shaders/PostProcessingVertex.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", compileFlags, 0, &postProcessingVertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders/PostProcessingPixel.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", compileFlags, 0, &postProcessingPixelShader, nullptr));
		// Atrous filter pass pso
		D3D12_GRAPHICS_PIPELINE_STATE_DESC postProcessingPSO = {};
		postProcessingPSO.InputLayout = { inputLightElementDescs, _countof(inputLightElementDescs) };
		postProcessingPSO.pRootSignature = m_rasterPostProcessingSignature.Get();
		postProcessingPSO.VS = CD3DX12_SHADER_BYTECODE(postProcessingVertexShader.Get());
		postProcessingPSO.PS = CD3DX12_SHADER_BYTECODE(postProcessingPixelShader.Get());
		postProcessingPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		postProcessingPSO.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		postProcessingPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		postProcessingPSO.SampleMask = UINT_MAX;
		postProcessingPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		postProcessingPSO.NumRenderTargets = 1;
		postProcessingPSO.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // m_rtvFormat;//DXGI_FORMAT_R8G8B8A8_UNORM
		postProcessingPSO.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&postProcessingPSO, IID_PPV_ARGS(&m_rasterPostProcessingPSO)));



	}

	//CS
	{

		std::vector<std::string> srv = { "normal","depth","velocity","prevDepth" };
		std::vector<std::string> uav = { "reprojection_map","test"};
		reprojectionMapCS = std::make_shared<CS>(m_device, L"shaders/reprojectionMapCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 0), true, GetStaticSamplers());
		reprojectionMapCS->srvResource = std::vector<std::string>{ "prevDepth" };
		reprojectionMapCS->uavResource = uav;

		srv = {};
		uav = { "brdf_fg" };
		brdfFgCS = std::make_shared<CS>(m_device, L"shaders/brdfFgCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0);
		brdfFgCS->srvResource = srv;
		brdfFgCS->uavResource = uav;

		srv = {};
		uav = { "sky_cube" };
		skyCubeCS = std::make_shared<CS>(m_device, L"shaders/skyCubeCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		skyCubeCS->srvResource = srv;
		skyCubeCS->uavResource = uav;

		srv = { "sky_cube" };
		uav = { "convolve_sky" };
		convolveSkyCS = std::make_shared<CS>(m_device, L"shaders/convolveSkyCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 0), true, GetStaticSamplers());
		convolveSkyCS->srvResource = srv;
		convolveSkyCS->uavResource = uav;


		srv = { "depth" };
		uav = { "prevDepth" };
		copyDepthCS = std::make_shared<CS>(m_device, L"shaders/copyDepthCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0);
		copyDepthCS->srvResource = std::vector<std::string>{ };
		copyDepthCS->uavResource = uav;

		srv = { "gbuffer" };
		uav = { "half_view_normal_tex" };
		extractViewNormalCS = std::make_shared<CS>(m_device, L"shaders/extractViewNormalCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		extractViewNormalCS->srvResource = std::vector<std::string>{ };
		extractViewNormalCS->uavResource = uav;

		srv = { "depth" };
		uav = { "half_depth_tex" };
		extractHalfDepthCS = std::make_shared<CS>(m_device, L"shaders/extractHalfDepthCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		extractHalfDepthCS->srvResource = std::vector<std::string>{ };
		extractHalfDepthCS->uavResource = uav;

		srv = { "gbuffer","half_depth_tex","half_view_normal_tex","accumImg","reprojection_map" };
		uav = { "in_ssgiTex" };
		ssaoCS = std::make_shared<CS>(m_device, L"shaders/ssaoCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		ssaoCS->srvResource = std::vector<std::string>{ "half_depth_tex","half_view_normal_tex","accumImg","reprojection_map" };
		ssaoCS->uavResource = uav;

		srv = { "in_ssgiTex","half_depth_tex","half_view_normal_tex" };
		uav = { "spatiallyFilteredTex" };
		ssaoSpatialCS = std::make_shared<CS>(m_device, L"shaders/ssaoSpatialCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0);
		ssaoSpatialCS->srvResource = srv;
		ssaoSpatialCS->uavResource = uav;

		srv = { "spatiallyFilteredTex","depth","gbuffer" };
		uav = { "upsampledTex" };
		ssaoUnsampleCS = std::make_shared<CS>(m_device, L"shaders/ssaoUnsampleCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0);
		ssaoUnsampleCS->srvResource = std::vector<std::string>{ "spatiallyFilteredTex" };
		ssaoUnsampleCS->uavResource = uav;

		srv = { "upsampledTex","reprojection_map" }; //ssaoTemporal_historyTex
		uav = { "ssgi_tex" }; //ssaoTemporal_historyOutputTex
		ssaoTemporalCS = std::make_shared<CS>(m_device, L"shaders/ssaoTemporalCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0, uint2(1, 1), true, GetStaticSamplers());
		ssaoTemporalCS->srvResource = std::vector<std::string>{ "upsampledTex","reprojection_map" ,"ssaoTemporal_historyTex" };
		ssaoTemporalCS->uavResource = std::vector<std::string>{ "ssgi_tex","ssaoTemporal_historyOutputTex" };

		srv = {};
		uav = {"ircache_pool_buf","ircache_life_buf"};
		clearIrcachePoolCS = std::make_shared<CS>(m_device, L"shaders/clearIrcachePoolCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0);
		clearIrcachePoolCS->srvResource = srv;
		clearIrcachePoolCS->uavResource = uav;

		srv = {}; //ircache_grid_meta_buf
		uav = { "ircache_entry_cell_buf","ircache_irradiance_buf","ircache_life_buf","ircache_pool_buf","ircache_meta_buf","test"}; //ircache_grid_meta_buf2
		scrollCascadesCS = std::make_shared<CS>(m_device, L"shaders/scrollCascadesCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(1, 1));
		scrollCascadesCS->srvResource = std::vector<std::string>{ "ircache_grid_meta_buf" };
		scrollCascadesCS->uavResource = std::vector<std::string>{ "ircache_entry_cell_buf","ircache_irradiance_buf","ircache_life_buf","ircache_pool_buf","ircache_meta_buf","ircache_grid_meta_buf2" };

		srv = { "ircache_meta_buf" };
		uav = { "_ircacheDispatchArgs" };
		_ircacheDispatchArgsCS = std::make_shared<CS>(m_device, L"shaders/_ircacheDispatchArgsCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		_ircacheDispatchArgsCS->srvResource = srv;
		_ircacheDispatchArgsCS->uavResource = uav;

		srv = {};
		uav = { "ircache_meta_buf" ,"ircache_entry_cell_buf","ircache_life_buf","ircache_pool_buf","ircache_spatial_buf",
			"ircache_reposition_proposal_buf","ircache_reposition_proposal_count_buf","ircache_irradiance_buf","entry_occupancy_buf"
		}; //"ircache_grid_meta_buf"
		ageIrcacheEntriesCS = std::make_shared<CS>(m_device, L"shaders/ageIrcacheEntriesCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 1));
		ageIrcacheEntriesCS->srvResource = srv;
		ageIrcacheEntriesCS->uavResource = std::vector<std::string>{ "ircache_meta_buf" ,"ircache_entry_cell_buf","ircache_life_buf","ircache_pool_buf","ircache_spatial_buf",
			"ircache_reposition_proposal_buf","ircache_reposition_proposal_count_buf","ircache_irradiance_buf","entry_occupancy_buf","ircache_grid_meta_buf" };

		srv = {};
		uav = { "entry_occupancy_buf" };
		_prefixScan1CS = std::make_shared<CS>(m_device, L"shaders/_prefixScan1CS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		_prefixScan1CS->srvResource = srv;
		_prefixScan1CS->uavResource = uav;

		srv = { "entry_occupancy_buf" };
		uav = { "segment_sum_buf" };
		_prefixScan2CS = std::make_shared<CS>(m_device, L"shaders/_prefixScan2CS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		_prefixScan2CS->srvResource = srv;
		_prefixScan2CS->uavResource = uav;

		srv = { "segment_sum_buf" };
		uav = { "entry_occupancy_buf" };
		_prefixScanMergeCS = std::make_shared<CS>(m_device, L"shaders/_prefixScanMergeCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		_prefixScanMergeCS->srvResource = srv;
		_prefixScanMergeCS->uavResource = uav;

		srv = { "entry_occupancy_buf" };
		uav = { "ircache_meta_buf","ircache_life_buf","ircache_entry_indirection_buf" };
		ircacheCompactCS = std::make_shared<CS>(m_device, L"shaders/ircacheCompactCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		ircacheCompactCS->srvResource = srv;
		ircacheCompactCS->uavResource = uav;

		srv = {};
		uav = { "ircache_meta_buf","_ircacheDispatchArgs2" };
		_ircacheDispatchArgsCS2 = std::make_shared<CS>(m_device, L"shaders/_ircacheDispatchArgsCS2.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		_ircacheDispatchArgsCS2->srvResource = srv;
		_ircacheDispatchArgsCS2->uavResource = uav;

		srv = { "ircache_life_buf","ircache_meta_buf","ircache_irradiance_buf","ircache_entry_indirection_buf" };
		uav = { "ircache_aux_buf" };
		ircacheResetCS = std::make_shared<CS>(m_device, L"shaders/ircacheResetCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		ircacheResetCS->srvResource = srv;
		ircacheResetCS->uavResource = uav;

		srv = { "reprojection_map" }; //temporal_history_tex
		uav = { "reprojected_history_tex" };
		rtdgiReprojectCS = std::make_shared<CS>(m_device, L"shaders/rtdgiReprojectCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(1, 0), true, GetStaticSamplers());
		rtdgiReprojectCS->srvResource = std::vector<std::string>{ "reprojection_map","temporal_history_tex" };
		rtdgiReprojectCS->uavResource = uav;

		srv = {"sunShadowMask"};
		uav = {"bitpacked_shadows_image"};
		shadowBitpackCS = std::make_shared<CS>(m_device, L"shaders/shadowBitpackCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0);
		shadowBitpackCS->srvResource = srv;
		shadowBitpackCS->uavResource = uav;

		srv = {"sunShadowMask","bitpacked_shadows_image","reprojection_map"};//prev_moments_image,prev_accum_image
		uav = {"denoised_shadow_mask","metadata_image"}; //moments_image
		shadowTemporalCS = std::make_shared<CS>(m_device, L"shaders/shadowTemporalCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1,uint2(2,1), true, GetStaticSamplers());
		shadowTemporalCS->srvResource = std::vector<std::string>{ "sunShadowMask","bitpacked_shadows_image","reprojection_map","prev_moments_image","prev_accum_image"};
		shadowTemporalCS->uavResource = std::vector<std::string>{ "denoised_shadow_mask","metadata_image","moments_image"};

		srv = {"metadata_image","normal","depth"};//1
		uav = {};//1
		shadowSpatialCS = std::make_shared<CS>(m_device, L"shaders/shadowSpatialCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 2, uint2(1, 1));
		shadowSpatialCS->srvResource = srv;
		shadowSpatialCS->uavResource = uav;

		srv = { "ircache_life_buf","ircache_entry_indirection_buf" };
		uav = { "ircache_meta_buf","ircache_irradiance_buf","ircache_aux_buf" };
		ircacheSumCS = std::make_shared<CS>(m_device, L"shaders/sumUpIrradianceCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		ircacheSumCS->srvResource = srv;
		ircacheSumCS->uavResource = uav;

		srv = { "ssgi_tex" };
		uav = { "half_ssao_tex" };
		extractHalfSSaoCS = std::make_shared<CS>(m_device, L"shaders/extractHalfSSaoCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1);
		extractHalfSSaoCS->srvResource = srv;
		extractHalfSSaoCS->uavResource = uav;

		srv = { "rt_history_validity_input_tex","reprojection_map","half_view_normal_tex","half_depth_tex" };// invalidity_history_tex
		uav = {};//invalidity_output_tex
		validityIntegrateCS = std::make_shared<CS>(m_device, L"shaders/validityIntegrateCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(1, 1));
		validityIntegrateCS->srvResource = std::vector<std::string>{ "rt_history_validity_input_tex","reprojection_map","half_view_normal_tex","half_depth_tex","invalidity_history_tex" };
		validityIntegrateCS->uavResource = std::vector<std::string>{ "invalidity_output_tex" };

		srv = { "half_view_normal_tex","depth","candidate_radiance_tex","candidate_normal_tex","candidate_hit_tex","reprojection_map" };
		//radiance_history_tex,ray_orig_history_tex,ray_history_tex,reservoir_history_tex,hit_normal_history_tex,candidate_history_tex,invalidity_output_tex
		uav = { "temporal_reservoir_packed_tex","test" };
		//radiance_output_tex,ray_orig_output_tex,ray_output_tex,hit_normal_output_tex,reservoir_output_tex,candidate_output_tex
		restirTemporalCS = std::make_shared<CS>(m_device, L"shaders/restirTemporalCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(7, 6), true, GetStaticSamplers(), MESH_TEXTURE_STARTINDEX);
		restirTemporalCS->srvResource = std::vector<std::string>{ "half_view_normal_tex","candidate_radiance_tex","candidate_normal_tex","candidate_hit_tex","reprojection_map" ,
		"radiance_history_tex","ray_orig_history_tex","ray_history_tex","reservoir_history_tex","hit_normal_history_tex","candidate_history_tex","invalidity_output_tex" };
		restirTemporalCS->uavResource = std::vector<std::string>{ "temporal_reservoir_packed_tex","radiance_output_tex","ray_orig_output_tex","ray_output_tex","hit_normal_output_tex",
		"reservoir_output_tex","candidate_output_tex"};

		srv = { "half_view_normal_tex","half_depth_tex","depth","half_ssao_tex","temporal_reservoir_packed_tex","reprojected_history_tex" };//reservoir_output_tex,radiance_output_tex
		uav = {};//reservoir_output_tex0,bounced_radiance_output_tex0
		restirSpatialCS = std::make_shared<CS>(m_device, L"shaders/restirSpatialCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 2, uint2(2, 2), true, GetStaticSamplers(), MESH_TEXTURE_STARTINDEX);
		restirSpatialCS->srvResource = std::vector<std::string>{ "half_view_normal_tex","half_depth_tex","half_ssao_tex","temporal_reservoir_packed_tex","reprojected_history_tex","reservoir_output_tex","radiance_output_tex" };
		restirSpatialCS->uavResource = std::vector<std::string>{ "reservoir_output_tex0","bounced_radiance_output_tex0" };

		srv = { "gbuffer","depth","half_view_normal_tex","half_depth_tex","ssgi_tex","candidate_radiance_tex",
		"candidate_hit_tex","temporal_reservoir_packed_tex" }; //radiance_output_tex,reservoir_output_tex1,bounced_radiance_output_tex1
		uav = { "irradiance_output_tex" };
		restirResolveCS = std::make_shared<CS>(m_device, L"shaders/restirResolveCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(3, 0), true, GetStaticSamplers(), MESH_TEXTURE_STARTINDEX);
		restirResolveCS->srvResource = std::vector<std::string>{ "half_view_normal_tex","half_depth_tex","ssgi_tex","candidate_radiance_tex",
		"candidate_hit_tex","temporal_reservoir_packed_tex","radiance_output_tex","reservoir_output_tex1","bounced_radiance_output_tex1" };
		restirResolveCS->uavResource = uav;

		srv = { "irradiance_output_tex","reprojected_history_tex","reprojection_map" };//variance_history_tex,invalidity_output_tex
		uav = { "temporal_filtered_tex" };//temporal_output_tex,temporal_variance_output_tex,
		rtdgiTemporalCS = std::make_shared<CS>(m_device, L"shaders/rtdgiTemporalCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(2, 2), true, GetStaticSamplers());
		rtdgiTemporalCS->srvResource = std::vector<std::string>{ "irradiance_output_tex","reprojected_history_tex","reprojection_map" ,"variance_history_tex","invalidity_output_tex" };
		rtdgiTemporalCS->uavResource = std::vector<std::string>{ "temporal_filtered_tex","temporal_output_tex","temporal_variance_output_tex" };

		srv = { "temporal_filtered_tex","depth","ssgi_tex","normal" };
		uav = { "spatial_filtered_tex" };
		rtdgiSpatialCS = std::make_shared<CS>(m_device, L"shaders/rtdgiSpatialCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 0), true, GetStaticSamplers());
		rtdgiSpatialCS->srvResource = std::vector<std::string>{ "temporal_filtered_tex","ssgi_tex" };
		rtdgiSpatialCS->uavResource = uav;

		srv = {"gbuffer","depth","denoised_shadow_mask","spatial_filtered_tex","ircache_spatial_buf","ircache_irradiance_buf",
		"wrc","sky_cube","convolve_sky"};
		uav = {"ircache_meta_buf","ircache_pool_buf","ircache_reposition_proposal_buf","ircache_reposition_proposal_count_buf",
		"ircache_entry_cell_buf","ircache_life_buf","accumImg","debug_out_tex"};//ircache_grid_meta_buf
		lightGbufferCS = std::make_shared<CS>(m_device, L"shaders/lightGbufferCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 1), true, GetStaticSamplers(), MESH_TEXTURE_STARTINDEX);
		lightGbufferCS->srvResource = std::vector<std::string>{ "denoised_shadow_mask","spatial_filtered_tex","ircache_spatial_buf","ircache_irradiance_buf",
		"wrc","sky_cube","convolve_sky" };
		lightGbufferCS->uavResource = std::vector<std::string>{ "ircache_meta_buf","ircache_pool_buf","ircache_reposition_proposal_buf","ircache_reposition_proposal_count_buf",
		"ircache_entry_cell_buf","ircache_life_buf","accumImg","debug_out_tex","ircache_grid_meta_buf"};

		srv = {};//"debug_out_tex"
		uav = { "output" };
		postCombineCS = std::make_shared<CS>(m_device, L"shaders/postCombineCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(1, 0), true, GetStaticSamplers(), MESH_TEXTURE_STARTINDEX);
		postCombineCS->srvResource = srv;
		postCombineCS->uavResource = {};

		srv = {"reservoir_history_tex","candidate_radiance_tex","invalidity_output_tex","radiance_output_tex","reservoir_output_tex1","irradiance_output_tex","temporal_filtered_tex","spatial_filtered_tex","debug_out_tex","test"};
		uav = { "output" };
		//LPCWSTR Shader_Macros[] = {L"1", L"FOUR=1"};
		finalCS = std::make_shared<CS>(m_device, L"shaders/finalCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 0), true, GetStaticSamplers());
		finalCS->srvResource = srv;
		finalCS->uavResource = std::vector<std::string>{};

		srv = {};
		uav = {"path_trace_tex"};
		clearPathTraceCS = std::make_shared<CS>(m_device, L"shaders/clearPathTraceCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0, uint2(0, 0));
		clearPathTraceCS->srvResource = srv;
		clearPathTraceCS->uavResource = uav;

		srv = {"reservoir_output_tex0"};
		uav = { "test" };
		copyTestCS = std::make_shared<CS>(m_device, L"shaders/copyTestCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0, uint2(0, 0));
		copyTestCS->srvResource = srv;
		copyTestCS->uavResource = uav;

		//Lumen

		srv = {"gbuffer","probe_radiance","probe_SH","normal","depth","my_probe_SH","probe_pre_direction"};
		uav = {"irradiance_output_tex","screen_tile_adaptive_probe_header","screen_tile_adaptive_probe_indices","adaptive_screen_probe_data","num_adaptive_screen_probes","screen_probe_scene_depth","test"};
		indirectCS = std::make_shared<CS>(m_device, L"shaders/indirectCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 2, uint2(0, 0));
		indirectCS->srvResource = { "probe_radiance" ,"probe_SH","my_probe_SH" };
		indirectCS->uavResource = uav;

		srv = {"normal","depth"};
		uav = { "screen_probe_scene_depth","test"};
		screenProbeDownSampleDepthCS = std::make_shared<CS>(m_device, L"shaders/screenProbeDownSampleDepthCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 0));
		screenProbeDownSampleDepthCS->srvResource = {};
		screenProbeDownSampleDepthCS->uavResource = uav;

		srv = {};
		uav = {"screen_tile_adaptive_probe_header","screen_tile_adaptive_probe_indices","adaptive_screen_probe_data","num_adaptive_screen_probes","RMSE_buf"};
		clearProbeCS = std::make_shared<CS>(m_device, L"shaders/clearProbeCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0, uint2(0, 0));
		clearProbeCS->srvResource = srv;
		clearProbeCS->uavResource = uav;

		srv = { "normal","depth","gbuffer"};
		uav = { "screen_tile_adaptive_probe_header","screen_tile_adaptive_probe_indices","adaptive_screen_probe_data","num_adaptive_screen_probes","screen_probe_scene_depth","test"};
		screenProbeAdaptivePlacementCS = std::make_shared<CS>(m_device, L"shaders/screenProbeAdaptivePlacementCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 2, uint2(0, 0));
		screenProbeAdaptivePlacementCS->srvResource = {};
		screenProbeAdaptivePlacementCS->uavResource = uav;

		srv = {"num_adaptive_screen_probes","gbuffer"};
		uav = {"probe_radiance","probe_SH","my_probe_SH"};
		screenProbeConvertToSHCS = std::make_shared<CS>(m_device, L"shaders/screenProbeConvertToSHCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 1, uint2(0, 0));
		screenProbeConvertToSHCS->srvResource = {};
		screenProbeConvertToSHCS->uavResource = uav;

		srv = { "probe_hit_distance","probe_pre_direction"};
		uav = { "probe_radiance","screen_tile_adaptive_probe_header","screen_tile_adaptive_probe_indices","adaptive_screen_probe_data","num_adaptive_screen_probes","screen_probe_scene_depth" };
		screenProbeFilterCS = std::make_shared<CS>(m_device, L"shaders/screenProbeFilterCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 2, uint2(0, 0));
		screenProbeFilterCS->srvResource = srv;
		screenProbeFilterCS->uavResource = uav;



		srv = { "irradiance_output_tex","reprojected_history_tex","reprojection_map" };//variance_history_tex,rt_history_validity_tex
		uav = { "temporal_filtered_tex_Lumen" };//temporal_output_tex,temporal_variance_output_tex,
		temporalCS = std::make_shared<CS>(m_device, L"shaders/temporalCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 2, uint2(2, 2), true, GetStaticSamplers());
		temporalCS->srvResource = std::vector<std::string>{ "irradiance_output_tex","reprojected_history_tex","reprojection_map" ,"variance_history_tex_Lumen","rt_history_validity_tex" };
		temporalCS->uavResource = std::vector<std::string>{ "debug_out_tex","temporal_output_tex","temporal_variance_output_tex_Lumen" };

		srv = { "gbuffer","depth","denoised_shadow_mask","temporal_filtered_tex_Lumen","sky_cube","convolve_sky" };
		uav = { "accumImg","debug_out_tex" };
		lightCS = std::make_shared<CS>(m_device, L"shaders/lightCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 2, uint2(0, 0), true, GetStaticSamplers(), MESH_TEXTURE_STARTINDEX);
		lightCS->srvResource = std::vector<std::string>{ "denoised_shadow_mask","temporal_filtered_tex_Lumen" };
		lightCS->uavResource = std::vector<std::string>{ "accumImg" };

		srv = {"debug_out_tex","path_trace_tex"};
		uav = {"RMSE"};
		calRMSECS = std::make_shared<CS>(m_device, L"shaders/calRMSECS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0, uint2(0, 0));
		calRMSECS->srvResource = srv;
		calRMSECS->uavResource = uav;

		srv = { "RMSE" };
		uav = { "RMSE_buf" };
		calRMSENumCS = std::make_shared<CS>(m_device, L"shaders/calRMSENumCS.hlsl", L"CS", getsrvHandle(srv), getuavHandle(uav), 0, uint2(0, 0));
		calRMSENumCS->srvResource = srv;
		calRMSENumCS->uavResource = uav;
	}
}

void RestirGI::CreateAllConstantBuffer()
{
	CreateSceneBuffer();
	CreateTemporalBuffer();
	CreateAtrousBuffer();
	CreatePostProcessingBuffer();
	CreateFrameBuffer();

	//CS cbuffer
	CreateShadowSpatialCB();
	CreateRestirSpatialCB();
	CreateFinalCB();
	CreateScreenProbeAdaptivePlacementCB();
	CreateIndirectCB();



}

void RestirGI::UpdateAllConstantBuffer()
{
	UpdateInstancePropertiesBuffer();

	UpdateSceneBuffer();
	UpdateTemporalBuffer();
	UpdatePostProcessingBuffer();
	UpdateFrameBuffer();

	UpdateFinalCB();
	UpdateIndirectCB();


}



void RestirGI::CreateSceneBuffer()
{
	auto compareMatrix = [](matrix A, matrix B) -> bool {
		auto compareFloat4 = [](XMFLOAT4 A, XMFLOAT4 B) ->bool {
			auto compareFloat = [](float A, float B) {
				return !(A > B || A < B);
			};
			return compareFloat(A.x, B.x) && compareFloat(A.y, B.y) && compareFloat(A.z, B.z) && compareFloat(A.w, B.w);
		};
		for (int i = 0; i < 4; ++i)
		{
			XMFLOAT4 a, b;
			XMStoreFloat4(&a, A.r[i]);
			XMStoreFloat4(&b, B.r[i]);
			if (compareFloat4(a, b) == false)
				return false;
		}
		return true;
	};
	m_sceneBuffer = std::make_unique<UploadBuffer<SceneParams>>(m_device.Get(), 1, true);
	m_sceneBuffer->Get()->SetName(L"sceneBuffer");

	//创建一个世界变换矩阵
	XMMATRIX world = worldTransform;
	m_rasterObjectCB = helper::CreateBuffer(m_device.Get(), sizeof(XMMATRIX), D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, helper::kUploadHeapProps);
	m_rasterObjectCB->SetName(L"rasterObjectCB");
	helper::CopyDataToUploadBuffer(m_rasterObjectCB.Get(), &world, sizeof(XMMATRIX));
}

void RestirGI::UpdateSceneBuffer()
{
	std::unique_ptr<SceneParams> sceneConstant = std::make_unique<SceneParams>();
	sceneConstant->lightPosition = m_lightPos;
	sceneConstant->time = ImGui::GetTime();
	sceneConstant->enableAO = m_enableAO;
	sceneConstant->showRenderType = m_showRenderType;
	sceneConstant->showRenderMode = m_showRenderMode;

	sceneConstant->probeRayRotation = float4(0, 0, 0, 0);
	if (importsampleRayGen)
		sceneConstant->patch1 = 10;
	else
		sceneConstant->patch1 = 0;
	sceneConstant->nearPlane = nearPlane;
	sceneConstant->farPlane = farPlane;
	sceneConstant->lightPower = lightPower;
	sceneConstant->offset = offset;


	m_sceneBuffer->CopyData(0, sceneConstant.get());
}

void RestirGI::CreateTemporalBuffer()
{
	m_temporalBuffer = std::make_unique<UploadBuffer<TemporalConstantBuffer>>(m_device.Get(), 1, true);
	m_temporalBuffer->Get()->SetName(L"temporalBuffer");
}

void RestirGI::UpdateTemporalBuffer()
{
	std::unique_ptr<TemporalConstantBuffer> TemporalConstant = std::make_unique<TemporalConstantBuffer>();
	TemporalConstant->bTemporal = m_bTemporal;
	m_temporalBuffer->CopyData(0, TemporalConstant.get());
}

void RestirGI::CreateAtrousBuffer()
{
	m_atrousBuffer = std::make_unique<UploadBuffer<AtrousConstantBuffer>>(m_device.Get(), 1, true);
	m_atrousBuffer->Get()->SetName(L"atrousBuffer");
}

void RestirGI::CreatePostProcessingBuffer()
{
	m_postProcessingBuffer = std::make_unique<UploadBuffer<PostProcessingConstantBuffer>>(m_device.Get(), 1, true);
	m_postProcessingBuffer->Get()->SetName(L"postProcessingBuffer");
}

void RestirGI::UpdatePostProcessingBuffer()
{
	std::unique_ptr<PostProcessingConstantBuffer> postProcessingConstant = std::make_unique<PostProcessingConstantBuffer>();
	postProcessingConstant->showRenderType = m_showRenderType;
	m_postProcessingBuffer->CopyData(0, postProcessingConstant.get());
}

void RestirGI::CreateFrameBuffer()
{
	m_frameBuffer = std::make_unique<UploadBuffer<FrameConstantBuffer>>(m_device.Get(), 1, true);
	m_frameBuffer->Get()->SetName(L"frameBuffer");

	auto radical_inverse = [](int n,int base)->float {
		float val = 0.0f;
		float inv_base = 1.0 / float(base);
		float inv_bi = inv_base;

		while (n > 0) {
			int d_i = n % base;
			val += float(d_i)  * inv_bi;
			n = int(float(n) * inv_base);
			inv_bi *= inv_base;
		}
		return val;

	};

	for (int i = 1; i <= 128; ++i)
	{
		supersample_offsets.push_back(float2(radical_inverse(i, 2) - 0.5, radical_inverse(i, 3) - 0.5));
	}



	
}

void RestirGI::UpdateFrameBuffer()
{
	std::unique_ptr<FrameConstantBuffer> frameConstant = std::make_unique<FrameConstantBuffer>();
	//
	nv_helpers_dx12::CameraManip.translate(m_keys, m_cameraMoveStep);


	//view_constants
	const glm::mat4& mat = nv_helpers_dx12::CameraManip.getMatrix();
	//matrices->view = mat;
	memcpy(&frameConstant->view_constants.world_to_view.r->m128_f32[0], glm::value_ptr(mat), 16 * sizeof(float));

	float fovAngleY = 68.0f * XM_PI / 180.0f;
#ifdef REVERSE_Z
	frameConstant->view_constants.view_to_clip = getReverseZ(fovAngleY, m_aspectRatio, nearPlane, farPlane);
#else
	frameConstant->view_constants.view_to_clip = DirectX::XMMatrixPerspectiveFovRH(fovAngleY, m_aspectRatio, nearPlane, farPlane);
#endif // !REVERSE_Z


	XMVECTOR det;
	frameConstant->view_constants.view_to_world = DirectX::XMMatrixInverse(&det, frameConstant->view_constants.world_to_view);
	frameConstant->view_constants.clip_to_view = DirectX::XMMatrixInverse(&det, frameConstant->view_constants.view_to_clip);

	frameConstant->view_constants.prev_world_to_prev_view = m_lastView;
	frameConstant->view_constants.prev_view_to_prev_clip = m_lastProjection;

	frameConstant->view_constants.prev_view_to_prev_world = DirectX::XMMatrixInverse(&det, frameConstant->view_constants.prev_world_to_prev_view);
	frameConstant->view_constants.prev_clip_to_prev_view = DirectX::XMMatrixInverse(&det, frameConstant->view_constants.prev_view_to_prev_clip);



	frameConstant->view_constants.clip_to_prev_clip = frameConstant->view_constants.clip_to_view
		* frameConstant->view_constants.view_to_world //上一帧和这一帧的世界矩阵是相同的
		* frameConstant->view_constants.prev_world_to_prev_view
		* frameConstant->view_constants.prev_view_to_prev_clip;
#ifdef USE_TAA_JITTER
	float2 sample_offset_pixels = supersample_offsets[frameTime % uint(supersample_offsets.size())];
#else
	float2 sample_offset_pixels = float2(0,0);
#endif
	
	float2 sample_offset_clip = float2((2.0 * sample_offset_pixels.x) / GetWidth(), (2.0 * sample_offset_pixels.y) / GetHeight());

	XMVECTOR sample_offset_clip_vec = XMLoadFloat2(&sample_offset_clip);
	XMVECTOR sample_offset_clip_invVec = XMLoadFloat2(&sample_offset_clip);
	sample_offset_clip_vec *= -1;
	sample_offset_clip_vec = XMVectorSetW(sample_offset_clip_vec, 1.0);
	sample_offset_clip_invVec = XMVectorSetW(sample_offset_clip_invVec, 1.0);

	matrix jitter_matrix = XMMatrixIdentity();
	matrix jitter_matrix_inv = XMMatrixIdentity();
	jitter_matrix.r[3] = sample_offset_clip_vec;
	jitter_matrix_inv.r[3] = sample_offset_clip_invVec;

	frameConstant->view_constants.view_to_sample =  frameConstant->view_constants.view_to_clip * jitter_matrix;
	frameConstant->view_constants.sample_to_view = jitter_matrix_inv * frameConstant->view_constants.clip_to_view;

	frameConstant->view_constants.sample_offset_pixels = sample_offset_pixels;
	frameConstant->view_constants.sample_offset_clip = sample_offset_clip;

	//
	glm::vec3 CameraPos = nv_helpers_dx12::CameraManip.getCameraPos();
	glm::vec3 SunPos = nv_helpers_dx12::CameraManip.getSun();
	float4 sun_direction = float4(SunPos.x, SunPos.y, SunPos.z, 0.f);
	XMVECTOR v = XMLoadFloat4(&sun_direction);
	v = XMVector3Normalize(v);
	XMStoreFloat4(&sun_direction, v);
	
	frameConstant->sun_direction = sun_direction;
	frameConstant->frame_index = frameTime;
	frameConstant->delta_time_seconds = 1/ ImGui::GetIO().Framerate; // ImGui::GetIO().Framerate
	frameConstant->sun_angular_radius_cos = 0.99999;
	frameConstant->triangle_light_count = 0;
	frameConstant->sun_color_multiplier = float4(1.5f * lightPower, 1.5f * lightPower, 1.5f * lightPower, 1.5f * lightPower);
	frameConstant->sky_ambient = float4(0.0, 0.0, 0.0, 0.0);
	frameConstant->pre_exposure = 1.0;
	frameConstant->pre_exposure_delta = 1.0;
	frameConstant->pre_exposure_prev = 1.0;

	frameConstant->pad0 = m_showRenderType;


	

	//render_overrides
	frameConstant->render_overrides.flags = 0;
	frameConstant->render_overrides.material_roughness_scale = 1.00;
	frameConstant->render_overrides.pad0 = 0;
	frameConstant->render_overrides.pad1 = 0;

	frameConstant->size.UINT_SIZE = sizeof(uint);
	frameConstant->size.UINT2_SIZE = sizeof(uint2);
	if(USE_SCREEN_GI_REPROJECTION)
		frameConstant->size.pad0 += 1 << 0;
	if (USE_IRCACHE)
		frameConstant->size.pad0 += 1 << 1;
	if(USE_EXPOSURE)
		frameConstant->size.pad0 += 1 << 2;
	if (USE_shotcut)
		frameConstant->size.pad0 += 1 << 3;
	if(USE_TONE_MAPPING)
		frameConstant->size.pad0 += 1 << 4;
	
	frameConstant->size.exposure = expousre;

	float3 eye_position = float3(CameraPos.x, CameraPos.y, CameraPos.z);
	int4 half = int4(IRCACHE_CASCADE_SIZE / 2, IRCACHE_CASCADE_SIZE / 2, IRCACHE_CASCADE_SIZE / 2, IRCACHE_CASCADE_SIZE / 2);
	XMVECTOR vEye = XMLoadFloat3(&eye_position);
	XMVECTOR vHalf = XMLoadSInt4(&half);
	
	

	frameConstant->ircache_grid_center = float4(CameraPos.x, CameraPos.y, CameraPos.z, 1.00);

	for (uint cascade = 0; cascade < 12; ++cascade)
	{
		float cell_diameter = IRCACHE_GRID_CELL_DIAMETER * float(1 << cascade) ;
		
		int3 cascade_center;
		int3 cascade_origin;
		XMVECTOR center = XMVectorFloor(vEye / cell_diameter);
		XMVECTOR origin = center - vHalf;
		XMStoreSInt3(&cascade_center, center);
		XMStoreSInt3(&cascade_origin, origin);
		prev_scroll[cascade] = cur_scroll[cascade];
		cur_scroll[cascade] = cascade_origin;
		int3 scroll_amount = int3(cur_scroll[cascade].x - prev_scroll[cascade].x, cur_scroll[cascade].y - prev_scroll[cascade].y, cur_scroll[cascade].z - prev_scroll[cascade].z);
		frameConstant->ircache_cascades[cascade].origin = int4(cur_scroll[cascade].x, cur_scroll[cascade].y, cur_scroll[cascade].z, 0);
		frameConstant->ircache_cascades[cascade].voxels_scrolled_this_frame = int4(scroll_amount.x, scroll_amount.y, scroll_amount.z, 0);
	}



	m_frameBuffer->CopyData(0, frameConstant.get());

	m_lastView = frameConstant->view_constants.world_to_view; //永远记录上一帧的view矩阵
	m_lastProjection = frameConstant->view_constants.view_to_clip;

}

void RestirGI::CreateShadowSpatialCB()
{
	m_shadowSpatialCB1 = std::make_unique<UploadBuffer<ShadowSpatialCB>>(m_device.Get(), 1, true);
	m_shadowSpatialCB1->Get()->SetName(L"m_shadowSpatialCB1");
	m_shadowSpatialCB2 = std::make_unique<UploadBuffer<ShadowSpatialCB>>(m_device.Get(), 1, true);
	m_shadowSpatialCB2->Get()->SetName(L"m_shadowSpatialCB2");
	m_shadowSpatialCB3 = std::make_unique<UploadBuffer<ShadowSpatialCB>>(m_device.Get(), 1, true);
	m_shadowSpatialCB3->Get()->SetName(L"m_shadowSpatialCB3");

	std::unique_ptr<ShadowSpatialCB> frameConstant = std::make_unique<ShadowSpatialCB>();
	frameConstant->input_tex_size = float4(GetWidth(), GetHeight(), 1 / float(GetWidth()), 1 / float(GetHeight()));
	frameConstant->bitpacked_shadow_mask_extent = uint2(GetWidth() / 8, GetHeight() / 4);
	frameConstant->step_size = 1;

	m_shadowSpatialCB1->CopyData(0, frameConstant.get());

	frameConstant->step_size = 2;
	m_shadowSpatialCB2->CopyData(0, frameConstant.get());

	frameConstant->step_size = 4;
	m_shadowSpatialCB3->CopyData(0, frameConstant.get());
}

void RestirGI::CreateRestirSpatialCB()
{
	m_restirSpatialCB1 = std::make_unique<UploadBuffer<RestirSpatialCB>>(m_device.Get(), 1, true);
	m_restirSpatialCB1->Get()->SetName(L"m_restirSpatialCB1");
	m_restirSpatialCB2 = std::make_unique<UploadBuffer<RestirSpatialCB>>(m_device.Get(), 1, true);
	m_restirSpatialCB2->Get()->SetName(L"m_restirSpatialCB2");

	std::unique_ptr<RestirSpatialCB> frameConstant = std::make_unique<RestirSpatialCB>();
	frameConstant->gbuffer_tex_size = float4(1920.0f, 1080.0f, 1 / 1920.0, 1 / 1080.0);
	frameConstant->output_tex_size = float4(960.0f, 540.0f, 1 / 960.0, 1 / 540.0);
	frameConstant->spatial_reuse_pass_idx = 0;
	frameConstant->perform_occlusion_raymarch = 0;
	frameConstant->occlusion_raymarch_importance_only = 0;

	m_restirSpatialCB1->CopyData(0, frameConstant.get());

	frameConstant->spatial_reuse_pass_idx = 1;
	frameConstant->perform_occlusion_raymarch = 1;
	frameConstant->occlusion_raymarch_importance_only = 0; //use_raytraced_reservoir_visibility
	m_restirSpatialCB2->CopyData(0, frameConstant.get());

}

void RestirGI::CreateFinalCB()
{
	m_finalCB = std::make_unique<UploadBuffer<FinalCB>>(m_device.Get(), 1, true);

	
}

void RestirGI::UpdateFinalCB()
{
	std::unique_ptr<FinalCB> frameConstant = std::make_unique<FinalCB>();
	frameConstant->control = uint(m_finalDebug);

	m_finalCB->CopyData(0, frameConstant.get());
}

void RestirGI::CreateIndirectCB()
{
	m_indirectCB = std::make_unique<UploadBuffer<IndirectCB>>(m_device.Get(), 1, true);


}

void RestirGI::UpdateIndirectCB()
{
	std::unique_ptr<IndirectCB> frameConstant = std::make_unique<IndirectCB>();
	frameConstant->showProbeRadiance = ShowProbeRadiance ? 1 : 0;
	frameConstant->enableFilter = EnableFilter ? 1 : 0;
	frameConstant->enableReservoir = EnableReservoir ? 1 : 0;
	frameConstant->depthDiff = depth_thresold;
	frameConstant->enableTemporal = Enabletemporal ? 1 : 0;

	m_indirectCB->CopyData(0, frameConstant.get());
}


void RestirGI::CreateScreenProbeAdaptivePlacementCB()
{
	m_screenProbeAdaptivePlacementCB1 = std::make_unique<UploadBuffer<ScreenProbeAdaptivePlacementCB>>(m_device.Get(), 1, true);
	m_screenProbeAdaptivePlacementCB1->Get()->SetName(L"m_screenProbeAdaptivePlacementCB1");
	m_screenProbeAdaptivePlacementCB2 = std::make_unique<UploadBuffer<ScreenProbeAdaptivePlacementCB>>(m_device.Get(), 1, true);
	m_screenProbeAdaptivePlacementCB2->Get()->SetName(L"m_screenProbeAdaptivePlacementCB2");

	std::unique_ptr<ScreenProbeAdaptivePlacementCB> frameConstant = std::make_unique<ScreenProbeAdaptivePlacementCB>();
	frameConstant->PlacementDownsampleFactor = 8;

	m_screenProbeAdaptivePlacementCB1->CopyData(0, frameConstant.get());

	frameConstant->PlacementDownsampleFactor = 4;

	m_screenProbeAdaptivePlacementCB2->CopyData(0, frameConstant.get());
}


void RestirGI::CheckRaytracingSupport()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
		&options5, sizeof(options5)));
	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		throw std::runtime_error("Raytracing not supported on device");

	//check shader model
	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_0 };
	ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)));
	if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0)
	{
#ifdef _DEBUG
		OutputDebugStringA("ERROR: Shader Model 6.0 is not supported!\n");
#endif
		throw std::exception("Shader Model 6.0 is not supported!");
	}
}

RestirGI::AccelerationStructureBuffers
RestirGI::CreateBottomLevelAS(
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers)
{
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;
	// Adding all vertex buffers and not transforming their position.
	for (size_t i = 0; i < vVertexBuffers.size(); i++) {
		if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
			bottomLevelAS.AddVertexBuffer(
				vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex_Model),
				vIndexBuffers[i].first.Get(), 0, vIndexBuffers[i].second, nullptr, 0, true);
		else
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex_Model), 0, 0);
	}

	UINT64 scratchSizeInBytes = 0;
	UINT64 resultSizeInBytes = 0;

	bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes);

	AccelerationStructureBuffers buffers;
	buffers.pScratch = helper::CreateBuffer(m_device.Get(), scratchSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, helper::kDefaultHeapProps);
	buffers.pResult = helper::CreateBuffer(m_device.Get(), resultSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, helper::kDefaultHeapProps);

	bottomLevelAS.Generate(m_commandList.Get(), buffers.pScratch.Get(), buffers.pResult.Get(), false, nullptr);

	return buffers;
}

void RestirGI::CreateTopLevelAS(
	const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances)
{
	// Gather all the instances into the builder helper
	for (size_t i = 0; i < instances.size(); i++) {
		m_topLevelASGenerator.AddInstance(instances[i].first.Get(),
			instances[i].second, static_cast<UINT>(i), static_cast<UINT>(2 * i));
	}

	UINT64 scratchSize, resultSize, instanceDescsSize;
	m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);

	m_topLevelASBuffers.pScratch = helper::CreateBuffer(m_device.Get(), scratchSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, helper::kDefaultHeapProps);
	m_topLevelASBuffers.pResult = helper::CreateBuffer(m_device.Get(), resultSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, helper::kDefaultHeapProps);
	m_topLevelASBuffers.pInstanceDesc = helper::CreateBuffer(m_device.Get(), instanceDescsSize,
		D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, helper::kUploadHeapProps);

	m_topLevelASGenerator.Generate(m_commandList.Get(), m_topLevelASBuffers.pScratch.Get(),
		m_topLevelASBuffers.pResult.Get(), m_topLevelASBuffers.pInstanceDesc.Get());
}

void RestirGI::CreateAccelerationStructures()
{
	// Build the bottom AS from the Triangle vertex buffer
	UINT meshCount = m_sceneModel.Meshes.size();
	//std::vector<AccelerationStructureBuffers> bottomLevelBuffers(meshCount);
	for (int i = 0; i < meshCount; ++i) {
		AccelerationStructureBuffers bottomLevelBuffers = CreateBottomLevelAS(
			{ {m_sceneModel.Meshes[i].first->VertexBufferGPU.Get(),m_sceneModel.Meshes[i].first->VertexCount} },
			{ {m_sceneModel.Meshes[i].first->IndexBufferGPU.Get(),m_sceneModel.Meshes[i].first->IndexCount} }
		);
		m_instances.push_back({ bottomLevelBuffers.pResult,worldTransform });
	}

	CreateTopLevelAS(m_instances);

	// Store the AS buffers. The rest of the buffers will be released once we exit the function
	//m_bottomLevelAS = bottomLevelBuffers.pResult;

}

RestirGI::RayTracingShaderLibrary
RestirGI::CreateRayTracingShaderLibrary(std::string name, LPCWSTR shadername,
	std::vector<std::wstring> exportSymbols, ComPtr<ID3D12RootSignature> signature)
{
	RestirGI::RayTracingShaderLibrary rtsl;
	rtsl.name = name;
	rtsl.library = helper::CompileShaderLibrary2(shadername);
	rtsl.exportSymbols = exportSymbols;
	rtsl.signature = signature;
	return rtsl;
}

void RestirGI::CreateRaytracingPipeline()
{
	auto staticSamplers = GetStaticSamplers();

	D3D12_ROOT_SIGNATURE_FLAGS flag = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	// lumen GI
	{
		LumenRT = std::make_shared<RT>();
		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());

		nv_helpers_dx12::RootSignatureGenerator globalRSG;
		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 1, 0);//constbuffer: frameconstants - b1,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({ {2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//reprojected_history_tex - t2,space0
		globalRSG.AddHeapRangesParameter({
		{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["MeshMaterialBuffer"].srvHandle}, // t0,space2
		{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["screen_probe_scene_depth"].srvHandle}, // t2-t7,space0
		{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["normal"].srvHandle}, 
		{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["depth"].srvHandle}, 
		{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["adaptive_screen_probe_data"].srvHandle}, 
		{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["num_adaptive_screen_probes"].srvHandle}, 
		{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_spatial_buf"].srvHandle},
		{9,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_irradiance_buf"].srvHandle},
		{10,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["convolve_sky"].srvHandle}, 
		{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//ircache_grid_meta_buf - u0,space1
		globalRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_radiance"].uavHandle} ,
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_hit_distance"].uavHandle},
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_reservoir"].uavHandle},//probe_vertex_packed 
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_vertex_packed"].uavHandle},
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_pre_direction"].uavHandle},//screen_probe_scene_history_depth
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["screen_probe_scene_history_depth"].uavHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_meta_buf"].uavHandle},
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_pool_buf"].uavHandle}, //uav
			{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_buf"].uavHandle},
			{9,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_count_buf"].uavHandle},
			{10,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_entry_cell_buf"].uavHandle},
			{11,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_life_buf"].uavHandle},
			{12,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["test"].uavHandle},//test
			
			}); 
	
		LumenRT->srv = std::vector<std::string>{"screen_probe_scene_depth","adaptive_screen_probe_data","num_adaptive_screen_probes","convolve_sky"};
		LumenRT->uav = std::vector<std::string>{"probe_radiance","probe_hit_distance","probe_reservoir","probe_vertex_packed","probe_pre_direction","screen_probe_scene_history_depth"};
		LumenRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		LumenRT->GlobalSignature->SetName(L"LumenRSG");

		pipeline.AddGlobalRootSignature(LumenRT->GlobalSignature.Get());



		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
				{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	u0,space2
			});

		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"lumenRayGen", L"shaders/LumenRayGen.hlsl", { L"LumenRayGen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));


		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"LumenRayGen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });


		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);

		LumenRT->StateObject = pipeline.Generate();
		LumenRT->StateObject->SetName(L"LumenPSO");
		ThrowIfFailed(LumenRT->StateObject->QueryInterface(IID_PPV_ARGS(&LumenRT->StateObjectProps)));

		m_rtShaderLibrary.clear();
	}

	//Lumen Vaildate
	{
		LumenVaildateRT = std::make_shared<RT>();
		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());

		nv_helpers_dx12::RootSignatureGenerator globalRSG;
		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 1, 0);//constbuffer: frameconstants - b1,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({ {2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//reprojected_history_tex - t2,space0
		globalRSG.AddHeapRangesParameter({
		{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["MeshMaterialBuffer"].srvHandle}, // t0,space2
		{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["probe_vertex_packed"].srvHandle}, // t2-t7,space0
		{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["normal"].srvHandle},
		{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["depth"].srvHandle},
		{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["adaptive_screen_probe_data"].srvHandle},
		{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["num_adaptive_screen_probes"].srvHandle},
		{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_spatial_buf"].srvHandle},
		{9,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_irradiance_buf"].srvHandle},
		{10,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["convolve_sky"].srvHandle},
		{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//ircache_grid_meta_buf - u0,space1
		globalRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_radiance"].uavHandle} ,
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_reservoir"].uavHandle},
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["test"].uavHandle},//test
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["probe_pre_direction"].uavHandle},//
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["rt_history_validity_tex"].uavHandle},
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_meta_buf"].uavHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_pool_buf"].uavHandle}, //uav
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_buf"].uavHandle},
			{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_count_buf"].uavHandle},
			{9,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_entry_cell_buf"].uavHandle},
			{10,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_life_buf"].uavHandle},

			});

		LumenVaildateRT->srv = std::vector<std::string>{ "probe_vertex_packed","adaptive_screen_probe_data","num_adaptive_screen_probes","convolve_sky" };
		LumenVaildateRT->uav = std::vector<std::string>{ "probe_radiance","probe_reservoir","probe_pre_direction","rt_history_validity_tex"};
		LumenVaildateRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		LumenVaildateRT->GlobalSignature->SetName(L"LumenVaildateRSG");

		pipeline.AddGlobalRootSignature(LumenVaildateRT->GlobalSignature.Get());



		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
				{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	u0,space2
			});

		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"lumenVaildateRgen", L"shaders/LumenVaildateRgen.hlsl", { L"LumenVaildateRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));


		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"LumenVaildateRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });


		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);

		LumenVaildateRT->StateObject = pipeline.Generate();
		LumenVaildateRT->StateObject->SetName(L"LumenVaildatePSO");
		ThrowIfFailed(LumenVaildateRT->StateObject->QueryInterface(IID_PPV_ARGS(&LumenVaildateRT->StateObjectProps)));

		m_rtShaderLibrary.clear();
	}
	//ircache trace access
	{
		ircacheTraceAccessRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;
		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({
			{0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_spatial_buf"].srvHandle}, //srv
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_life_buf"].srvHandle},
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_meta_buf"].srvHandle},
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_entry_indirection_buf"].srvHandle},
			});
		globalRSG.AddHeapRangesParameter({
			{0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_buf"].uavHandle}, //uav
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_aux_buf"].uavHandle},
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["test"].uavHandle},//test
			});
		ircacheTraceAccessRT->srv = std::vector<std::string>{ "ircache_spatial_buf" ,"ircache_life_buf" ,"ircache_meta_buf" ,"ircache_entry_indirection_buf" };
		ircacheTraceAccessRT->uav = std::vector<std::string>{ "ircache_reposition_proposal_buf" ,"ircache_aux_buf" };
		ircacheTraceAccessRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		ircacheTraceAccessRT->GlobalSignature->SetName(L"ircacheTraceAccessRSG");

		pipeline.AddGlobalRootSignature(ircacheTraceAccessRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
		{0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"ircacheTraceAccessRayGen", L"shaders/TraceAccessibilityRgen.hlsl", { L"TraceAccessibilityRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"TraceAccessibilityRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });

		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		ircacheTraceAccessRT->StateObject = pipeline.Generate();
		ircacheTraceAccessRT->StateObject->SetName(L"ircacheTraceAccessPSO");
		ThrowIfFailed(ircacheTraceAccessRT->StateObject->QueryInterface(IID_PPV_ARGS(&ircacheTraceAccessRT->StateObjectProps)));


		m_rtShaderLibrary.clear();
	}

	//ircache validate	
	{
		ircacheVaildateRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;
		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({
			{1,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_spatial_buf"].srvHandle}, //t1-t4,space1
			{2,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["wrc"].srvHandle},
			{3,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_entry_indirection_buf"].srvHandle},
			{4,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["convolve_sky"].srvHandle},
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["MeshMaterialBuffer"].srvHandle},
			{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//ircache_grid_meta_buf
		globalRSG.AddHeapRangesParameter({
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_life_buf"].uavHandle},
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_buf"].uavHandle}, //uav
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_count_buf"].uavHandle},
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_meta_buf"].uavHandle},
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_aux_buf"].uavHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_pool_buf"].uavHandle},
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_entry_cell_buf"].uavHandle},
			});
		ircacheVaildateRT->srv = std::vector<std::string>{ "ircache_spatial_buf","wrc","ircache_entry_indirection_buf","convolve_sky"};
		ircacheVaildateRT->uav = std::vector<std::string>{ "ircache_grid_meta_buf","ircache_life_buf","ircache_reposition_proposal_buf","ircache_reposition_proposal_count_buf",
		"ircache_meta_buf","ircache_aux_buf","ircache_pool_buf","ircache_entry_cell_buf"
		};
		ircacheVaildateRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		ircacheVaildateRT->GlobalSignature->SetName(L"ircacheVaildateRSG");

		pipeline.AddGlobalRootSignature(ircacheVaildateRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
{0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"ircacheValidateRayGen", L"shaders/ircacheValidateRgen.hlsl", { L"IrcacheValidateRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));

		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"IrcacheValidateRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });

		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		ircacheVaildateRT->StateObject = pipeline.Generate();
		ircacheVaildateRT->StateObject->SetName(L"ircacheVaildatePSO");
		ThrowIfFailed(ircacheVaildateRT->StateObject->QueryInterface(IID_PPV_ARGS(&ircacheVaildateRT->StateObjectProps)));

		m_rtShaderLibrary.clear();
	}

	//ircache trace
	{
		ircacheTraceRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;

		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({
			{1,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_spatial_buf"].srvHandle}, //t1-t4,space1
			{2,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["wrc"].srvHandle},
			{3,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_entry_indirection_buf"].srvHandle},
			{4,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["convolve_sky"].srvHandle},
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["MeshMaterialBuffer"].srvHandle},
			{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//ircache_grid_meta_buf
		globalRSG.AddHeapRangesParameter({
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_life_buf"].uavHandle},
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_buf"].uavHandle}, //uav
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_count_buf"].uavHandle},
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_meta_buf"].uavHandle},
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_aux_buf"].uavHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_pool_buf"].uavHandle},
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_entry_cell_buf"].uavHandle},
			});
		ircacheTraceRT->srv = std::vector<std::string>{ "ircache_spatial_buf","wrc","ircache_entry_indirection_buf","convolve_sky"};
		ircacheTraceRT->uav = std::vector<std::string>{ "ircache_grid_meta_buf","ircache_life_buf","ircache_reposition_proposal_buf","ircache_meta_buf",
		"ircache_aux_buf","ircache_pool_buf","ircache_entry_cell_buf" };

		ircacheTraceRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		ircacheTraceRT->GlobalSignature->SetName(L"ircacheTraceRSG");

		pipeline.AddGlobalRootSignature(ircacheTraceRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
{0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"traceIrradianceRgen", L"shaders/TraceIrradianceRgen.hlsl", { L"TraceIrradianceRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));

		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"TraceIrradianceRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });

		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		ircacheTraceRT->StateObject = pipeline.Generate();
		ircacheTraceRT->StateObject->SetName(L"ircacheTracePSO");
		ThrowIfFailed(ircacheTraceRT->StateObject->QueryInterface(IID_PPV_ARGS(&ircacheTraceRT->StateObjectProps)));

		m_rtShaderLibrary.clear();
	}

	//trace shadow mask
	{
		traceShadowMaskRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;
		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({
			{0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["depth"].srvHandle}, //srv
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["normal"].srvHandle},
			{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({
			{0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["sunShadowMask"].uavHandle}, //uav

			});
		traceShadowMaskRT->srv = std::vector<std::string>{};
		traceShadowMaskRT->uav = std::vector<std::string>{ "sunShadowMask" };

		traceShadowMaskRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		traceShadowMaskRT->GlobalSignature->SetName(L"traceShadowRSG");

		pipeline.AddGlobalRootSignature(traceShadowMaskRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
{0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"traceSunShadowMaskRgen", L"shaders/TraceSunShadowMaskRgen.hlsl", { L"TraceSunShadowMaskRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"TraceSunShadowMaskRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });

		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		traceShadowMaskRT->StateObject = pipeline.Generate();
		traceShadowMaskRT->StateObject->SetName(L"traceShadowPSO");
		ThrowIfFailed(traceShadowMaskRT->StateObject->QueryInterface(IID_PPV_ARGS(&traceShadowMaskRT->StateObjectProps)));


		m_rtShaderLibrary.clear();
	}

	//rtdgi validate
	{
		rtdgiValidateRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;

		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({ {1,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//reprojected_history_tex - t1,space1
		globalRSG.AddHeapRangesParameter({ {2,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//ray_history_tex - t2,space1
		globalRSG.AddHeapRangesParameter({ {3,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//ray_orig_history_tex - t3,space1
		globalRSG.AddHeapRangesParameter({
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["half_view_normal_tex"].srvHandle}, //t2-t7,space0
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["depth"].srvHandle},
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["reprojection_map"].srvHandle},
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_spatial_buf"].srvHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_irradiance_buf"].srvHandle},
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["wrc"].srvHandle},
			{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["convolve_sky"].srvHandle},
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["MeshMaterialBuffer"].srvHandle}, //MeshMaterialBuffer - t0,space2
			{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//ircache_grid_meta_buf - u0,space1
		globalRSG.AddHeapRangesParameter({ {1,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//reservoir_history_tex - u1,space1
		globalRSG.AddHeapRangesParameter({ {2,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//radiance_history_tex - u2,space1
		globalRSG.AddHeapRangesParameter({
			{0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_meta_buf"].uavHandle},
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_pool_buf"].uavHandle}, //uav
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_buf"].uavHandle},
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_count_buf"].uavHandle},
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_entry_cell_buf"].uavHandle},
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_life_buf"].uavHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["rt_history_validity_pre_input_tex"].uavHandle},
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["testBuffer"].uavHandle},
			{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["test"].uavHandle},
			});
		rtdgiValidateRT->srv = std::vector<std::string>{ "half_view_normal_tex","reprojection_map","ircache_spatial_buf","ircache_irradiance_buf","wrc","convolve_sky",
		"reprojected_history_tex","ray_history_tex","ray_orig_history_tex"};
		rtdgiValidateRT->uav = std::vector<std::string>{ "ircache_grid_meta_buf","reservoir_history_tex","radiance_history_tex",
		"ircache_meta_buf","ircache_pool_buf","ircache_reposition_proposal_buf","ircache_reposition_proposal_count_buf","ircache_entry_cell_buf",
		"ircache_life_buf","rt_history_validity_pre_input_tex" };

		rtdgiValidateRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		rtdgiValidateRT->GlobalSignature->SetName(L"rtdgiValidateRSG");

		pipeline.AddGlobalRootSignature(rtdgiValidateRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
		{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"rtdgiValidateRgen", L"shaders/RtdgiValidateRgen.hlsl", { L"RtdgiValidateRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));

		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"RtdgiValidateRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });

		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		rtdgiValidateRT->StateObject = pipeline.Generate();
		rtdgiValidateRT->StateObject->SetName(L"rtdgiValidatePSO");
		ThrowIfFailed(rtdgiValidateRT->StateObject->QueryInterface(IID_PPV_ARGS(&rtdgiValidateRT->StateObjectProps)));

		m_rtShaderLibrary.clear();
	}

	//rtdgi trace
	{
		rtdgiTraceRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;

		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({ {1,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//ray_orig_history_tex - t1,space1
		globalRSG.AddHeapRangesParameter({
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["half_view_normal_tex"].srvHandle}, //t2-t9,space0
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["depth"].srvHandle},
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["reprojected_history_tex"].srvHandle},
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["reprojection_map"].srvHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["rt_history_validity_pre_input_tex"].srvHandle},
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_spatial_buf"].srvHandle},
			{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["ircache_irradiance_buf"].srvHandle},
			{9,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["wrc"].srvHandle},
			{10,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["worldPositon"].srvHandle},
			{11,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["convolve_sky"].srvHandle},
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["MeshMaterialBuffer"].srvHandle}, //MeshMaterialBuffer - t0,space2
			{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0} });//ircache_grid_meta_buf - u0,space1
		globalRSG.AddHeapRangesParameter({
			{0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_meta_buf"].uavHandle},
			{1,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_pool_buf"].uavHandle}, //uav
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_buf"].uavHandle},
			{3,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_reposition_proposal_count_buf"].uavHandle},
			{4,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_entry_cell_buf"].uavHandle},
			{5,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["ircache_life_buf"].uavHandle},
			{6,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["candidate_radiance_tex"].uavHandle},
			{7,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["candidate_normal_tex"].uavHandle},
			{8,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["candidate_hit_tex"].uavHandle},
			{9,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["rt_history_validity_input_tex"].uavHandle},
			{10,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["test"].uavHandle},
			});

		rtdgiTraceRT->srv = std::vector<std::string>{ "half_view_normal_tex","reprojected_history_tex","reprojection_map","rt_history_validity_pre_input_tex",
		"ircache_spatial_buf","ircache_irradiance_buf","wrc","convolve_sky","ray_orig_history_tex"};
		rtdgiTraceRT->uav = std::vector<std::string>{ "ircache_grid_meta_buf","ircache_meta_buf","ircache_pool_buf","ircache_reposition_proposal_buf",
		"ircache_reposition_proposal_count_buf","ircache_entry_cell_buf","ircache_life_buf","candidate_radiance_tex","candidate_normal_tex","candidate_hit_tex","rt_history_validity_input_tex" };

		rtdgiTraceRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		rtdgiTraceRT->GlobalSignature->SetName(L"rtdgiTraceRSG");

		pipeline.AddGlobalRootSignature(rtdgiTraceRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"rtdgiTraceRgen", L"shaders/RtdgiTraceRgen.hlsl", { L"RtdgiTraceRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));


		//shadowrays - 4
		nv_helpers_dx12::RootSignatureGenerator shadowrayRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"MyFirstShadowRays", L"shaders/ShadowRay.hlsl", { L"ShadowClosestHit", L"ShadowMiss" }, shadowrayRSG.Generate(m_device.Get(), flag)));

		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");
		pipeline.AddHitGroup(L"ShadowHitGroup", L"ShadowClosestHit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"RtdgiTraceRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[4].signature.Get(), { L"ShadowMiss",L"ShadowHitGroup" });

		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		rtdgiTraceRT->StateObject = pipeline.Generate();
		rtdgiTraceRT->StateObject->SetName(L"rtdgiTracePSO");
		ThrowIfFailed(rtdgiTraceRT->StateObject->QueryInterface(IID_PPV_ARGS(&rtdgiTraceRT->StateObjectProps)));

		m_rtShaderLibrary.clear();

	}

	//reflection trace
	if(false)
	{
		reflectionTraceRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;

		reflectionTraceRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		reflectionTraceRT->GlobalSignature->SetName(L"reflectionTraceRSG");

		pipeline.AddGlobalRootSignature(reflectionTraceRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"reflectionTraceRgen", L"shaders/ReflectionTraceRgen.hlsl", { L"ReflectionTraceRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));


		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"ReflectionTraceRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });

		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		reflectionTraceRT->StateObject = pipeline.Generate();
		reflectionTraceRT->StateObject->SetName(L"reflectionTracePSO");
		ThrowIfFailed(reflectionTraceRT->StateObject->QueryInterface(IID_PPV_ARGS(&reflectionTraceRT->StateObjectProps)));

		m_rtShaderLibrary.clear();
	}

	//path tracing
	{
		pathTraceRT = std::make_shared<RT>();

		nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());
		nv_helpers_dx12::RootSignatureGenerator globalRSG;

		globalRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0);//constbuffer: frameconstants - b0,space0
		globalRSG.AddHeapRangesParameter({ {0,1,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,0} });//acceleration struction - t0,space1
		globalRSG.AddHeapRangesParameter({
			{2,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["convolve_sky"].srvHandle}, //t2,space0
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,TexMap["MeshMaterialBuffer"].srvHandle}, // t1,space0
			{1,MY_TEXTURE_2D_BINDLESS_TABLE_SIZE,2,D3D12_DESCRIPTOR_RANGE_TYPE_SRV,MESH_TEXTURE_STARTINDEX}, //bindless - t1,space2
			});
		globalRSG.AddHeapRangesParameter({ {0,1,0,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,TexMap["path_trace_tex"].uavHandle} });//ircache_grid_meta_buf - u0,space1

		pathTraceRT->srv = std::vector<std::string>{ };
		pathTraceRT->uav = std::vector<std::string>{ "path_trace_tex" };

		pathTraceRT->GlobalSignature = globalRSG.Generate(m_device.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE, (UINT)staticSamplers.size(), staticSamplers.data());
		pathTraceRT->GlobalSignature->SetName(L"pathTraceRSG");

		pipeline.AddGlobalRootSignature(pathTraceRT->GlobalSignature.Get());

		//create loacl signature
		//raygens - 0
		nv_helpers_dx12::RootSignatureGenerator raygenRSG;
		raygenRSG.AddHeapRangesParameter({
			{0,1,2,D3D12_DESCRIPTOR_RANGE_TYPE_UAV,0 },//output resource	
			});
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"pathTraceRgen", L"shaders/PathTraceRgen.hlsl", { L"PathTraceRgen" }, raygenRSG.Generate(m_device.Get(), flag)));

		//shadow miss - 1
		nv_helpers_dx12::RootSignatureGenerator shadowMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"shadow_rmiss", L"shaders/ShadowRmiss.hlsl", { L"ShadowRmiss" }, shadowMissRSG.Generate(m_device.Get(), flag)));

		//gbuffer miss - 2
		nv_helpers_dx12::RootSignatureGenerator gbufferMissRSG;
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rmiss", L"shaders/GbufferRMiss.hlsl", { L"GbufferRMiss" }, gbufferMissRSG.Generate(m_device.Get(), flag)));

		//hit -3
		nv_helpers_dx12::RootSignatureGenerator rchitRSG;
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // vertex
		rchitRSG.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // indices
		m_rtShaderLibrary.push_back(CreateRayTracingShaderLibrary(
			"gbuffer_rchit", L"shaders/GbufferRchit.hlsl", { L"GbufferRchit" }, rchitRSG.Generate(m_device.Get(), flag)));



		for (auto& lib : m_rtShaderLibrary) {
			pipeline.AddLibrary(lib.library.Get(), lib.exportSymbols);
		}

		pipeline.AddHitGroup(L"rHitGroup", L"GbufferRchit");

		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[0].signature.Get(), { L"PathTraceRgen" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[2].signature.Get(), { L"GbufferRMiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[1].signature.Get(), { L"ShadowRmiss" });
		pipeline.AddRootSignatureAssociation(m_rtShaderLibrary[3].signature.Get(), { L"rHitGroup" });


		pipeline.SetMaxPayloadSize(3 * sizeof(float) + 5 * sizeof(UINT)); // GbufferRayPayload
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates
		pipeline.SetMaxRecursionDepth(2);
		pathTraceRT->StateObject = pipeline.Generate();
		pathTraceRT->StateObject->SetName(L"pathTracePSO");
		ThrowIfFailed(pathTraceRT->StateObject->QueryInterface(IID_PPV_ARGS(&pathTraceRT->StateObjectProps)));

		m_rtShaderLibrary.clear();
	}
}

void RestirGI::CreateRayTracingResource()
{
	m_rtSrvUavHeap = helper::CreateDescriptorHeap(m_device.Get(), 5000, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
	//create output buffer
	{
		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.DepthOrArraySize = 1;
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resDesc.Width = GetWidth();
		resDesc.Height = GetHeight();
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resDesc.MipLevels = 1;
		resDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateCommittedResource(&helper::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_outputResource)));

		m_outputResource->SetName(L"output");


	}



	//create shader resource heap
	{

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc, srvHandle);

		srvHandle.Offset(1, m_cbvSrvUavDescriptorSize);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
		m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

		//srvHandle.Offset(1, m_cbvSrvUavDescriptorSize);
		//D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		//cbvDesc.BufferLocation = m_cameraBuffer->Get()->GetGPUVirtualAddress();
		//cbvDesc.SizeInBytes = m_cameraBuffer->GetBufferSize();
		//m_device->CreateConstantBufferView(&cbvDesc, srvHandle);

		srvHandle.Offset(1, m_cbvSrvUavDescriptorSize);
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc1 = {};
		cbvDesc1.BufferLocation = m_sceneBuffer->Get()->GetGPUVirtualAddress();
		cbvDesc1.SizeInBytes = m_sceneBuffer->GetBufferSize();
		m_device->CreateConstantBufferView(&cbvDesc1, srvHandle);



		//add texutre srv to heap
		srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		srvHandle.Offset(MESH_TEXTURE_STARTINDEX, m_cbvSrvUavDescriptorSize);
		m_textloader.AddDescriptorToHeap(srvHandle, MESH_TEXTURE_STARTINDEX);

		srvHandle.Offset(199, m_cbvSrvUavDescriptorSize);
		m_ddsloader.AddDescriptorToHeap(srvHandle, MESH_TEXTURE_STARTINDEX + 199);


	}
}

void RestirGI::CreateShaderBindingTable()
{
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	auto heapPointer = reinterpret_cast<UINT64*>(srvUavHeapHandle.ptr);
	//Lumen GI
	{
		LumenRT->SBTHelper.Reset();

		LumenRT->SBTHelper.AddRayGenerationProgram(L"LumenRayGen", { heapPointer });
		LumenRT->SBTHelper.AddMissProgram(L"GbufferRMiss", {});
		LumenRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		for (int i = 0; i < m_sceneModel.Meshes.size(); ++i) {
			LumenRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			LumenRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
		}

		LumenRT->generate(m_device);
	}

	//LumenVaildateRgen
	{
		LumenVaildateRT->SBTHelper.Reset();

		LumenVaildateRT->SBTHelper.AddRayGenerationProgram(L"LumenVaildateRgen", { heapPointer });
		LumenVaildateRT->SBTHelper.AddMissProgram(L"GbufferRMiss", {});
		LumenVaildateRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		for (int i = 0; i < m_sceneModel.Meshes.size(); ++i) {
			LumenVaildateRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			LumenVaildateRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
		}

		LumenVaildateRT->generate(m_device);
	}
	//ircache trace access
	{
		ircacheTraceAccessRT->SBTHelper.Reset();

		ircacheTraceAccessRT->SBTHelper.AddRayGenerationProgram(L"TraceAccessibilityRgen", { heapPointer });
		ircacheTraceAccessRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});
		ircacheTraceAccessRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		ircacheTraceAccessRT->generate(m_device);
	}

	//ircache validate
	{
		ircacheVaildateRT->SBTHelper.Reset();

		ircacheVaildateRT->SBTHelper.AddRayGenerationProgram(L"IrcacheValidateRgen", { heapPointer });
		ircacheVaildateRT->SBTHelper.AddMissProgram(L"GbufferRMiss", {});
		ircacheVaildateRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		for (int i = 0; i < m_sceneModel.Meshes.size(); ++i) {
			ircacheVaildateRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			ircacheVaildateRT->SBTHelper.AddHitGroup(L"rHitGroup", {
(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
		}

		ircacheVaildateRT->generate(m_device);
	}

	//ircache trace
	{
		ircacheTraceRT->SBTHelper.Reset();

		ircacheTraceRT->SBTHelper.AddRayGenerationProgram(L"TraceIrradianceRgen", { heapPointer });
		ircacheTraceRT->SBTHelper.AddMissProgram(L"GbufferRMiss", {});
		ircacheTraceRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		for (int i = 0; i < m_sceneModel.Meshes.size(); ++i) {
			ircacheTraceRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			ircacheTraceRT->SBTHelper.AddHitGroup(L"rHitGroup", {
(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
		}

		ircacheTraceRT->generate(m_device);
	}

	//trace shadow mask
	{
		traceShadowMaskRT->SBTHelper.Reset();

		traceShadowMaskRT->SBTHelper.AddRayGenerationProgram(L"TraceSunShadowMaskRgen", { heapPointer });
		traceShadowMaskRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});
		traceShadowMaskRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		traceShadowMaskRT->generate(m_device);
	}

	//rtdgi validate
	{
		rtdgiValidateRT->SBTHelper.Reset();

		rtdgiValidateRT->SBTHelper.AddRayGenerationProgram(L"RtdgiValidateRgen", { heapPointer });
		rtdgiValidateRT->SBTHelper.AddMissProgram(L"GbufferRMiss", {});
		rtdgiValidateRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		for (int i = 0; i < m_sceneModel.Meshes.size(); ++i) {
			rtdgiValidateRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			rtdgiValidateRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
		}

		rtdgiValidateRT->generate(m_device);
	}

	//rtdgi trace
	{
		rtdgiTraceRT->SBTHelper.Reset();

		rtdgiTraceRT->SBTHelper.AddRayGenerationProgram(L"RtdgiTraceRgen", { heapPointer });
		rtdgiTraceRT->SBTHelper.AddMissProgram(L"GbufferRMiss", {});
		rtdgiTraceRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		for (int i = 0; i < m_sceneModel.Meshes.size(); ++i) {
			rtdgiTraceRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			rtdgiTraceRT->SBTHelper.AddHitGroup(L"rHitGroup", {
			(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
			(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			//rtdgiTraceRT->SBTHelper.AddHitGroup(L"ShadowHitGroup", {});

		}

		rtdgiTraceRT->generate(m_device);
	}

	//reflection trace
	{

	}
	//path tracing
	{
		pathTraceRT->SBTHelper.Reset();

		pathTraceRT->SBTHelper.AddRayGenerationProgram(L"PathTraceRgen", { heapPointer });
		pathTraceRT->SBTHelper.AddMissProgram(L"GbufferRMiss", {});
		pathTraceRT->SBTHelper.AddMissProgram(L"ShadowRmiss", {});

		for (int i = 0; i < m_sceneModel.Meshes.size(); ++i) {
			pathTraceRT->SBTHelper.AddHitGroup(L"rHitGroup", {
						(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
						(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			pathTraceRT->SBTHelper.AddHitGroup(L"rHitGroup", {
			(void*)(m_sceneModel.Meshes[i].first->VertexBufferGPU->GetGPUVirtualAddress()),
			(void*)(m_sceneModel.Meshes[i].first->IndexBufferGPU->GetGPUVirtualAddress()),
				});
			//rtdgiTraceRT->SBTHelper.AddHitGroup(L"ShadowHitGroup", {});

		}

		pathTraceRT->generate(m_device);
	}
}

void RestirGI::CreateInstancePropertiesBuffer() {
	m_instanceProperties = std::make_unique<UploadBuffer<InstanceProperties>>(m_device.Get(), m_instances.size(), true);
}

void RestirGI::UpdateInstancePropertiesBuffer() {
	InstanceProperties* current = new InstanceProperties;

	int index = 0;
	for (const auto& inst : m_instances) {
		current->objectToWorld = inst.second;
		//# DXR Extra - Simple Lighting 
		XMMATRIX upper3x3 = inst.second;
		// Remove the translation and lower vector of the matrix 
		upper3x3.r[0].m128_f32[3] = 0.f;
		upper3x3.r[1].m128_f32[3] = 0.f;
		upper3x3.r[2].m128_f32[3] = 0.f;
		upper3x3.r[3].m128_f32[0] = 0.f;
		upper3x3.r[3].m128_f32[1] = 0.f;
		upper3x3.r[3].m128_f32[2] = 0.f;
		upper3x3.r[3].m128_f32[3] = 1.f;
		XMVECTOR det;
		current->objectToWorldNormal = XMMatrixTranspose(XMMatrixInverse(&det, upper3x3));
		m_instanceProperties->CopyData(index++, current);
		//current++;
	}
	delete current;
}

void RestirGI::CalculateFrameStats()
{
	static int frameCnt = 0;
	static double elapsedTime = 0.0f;
	double totalTime = ImGui::GetTime();
	frameCnt++;
	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f)
	{
		float diff = static_cast<float>(totalTime - elapsedTime);
		float fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

		frameCnt = 0;
		elapsedTime = totalTime;

		float MRaysPerSecond = (m_width * m_height * fps) / static_cast<float>(1e6);

		std::wstringstream windowText;
		windowText << L"(DXR)";

		windowText << std::setprecision(2) << std::fixed
			<< L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
			<< L"    GPU[" << m_adapterID << L"]: " << m_adapterDescWString;

		m_info = windowText.str();
		//SetCustomWindowText(m_strInfo.c_str());
	}
}

std::array<CD3DX12_STATIC_SAMPLER_DESC, 6> RestirGI::GetStaticSamplers()
{
	//过滤器POINT,寻址模式WRAP的静态采样器
	CD3DX12_STATIC_SAMPLER_DESC pointWarp(0,	//着色器寄存器
		D3D12_FILTER_MIN_MAG_MIP_POINT,		//过滤器类型为POINT(常量插值)
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U方向上的寻址模式为WRAP（重复寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V方向上的寻址模式为WRAP（重复寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W方向上的寻址模式为WRAP（重复寻址模式）

	//过滤器POINT,寻址模式CLAMP的静态采样器
	CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(1,	//着色器寄存器
		D3D12_FILTER_ANISOTROPIC,		//过滤器类型为各向异性过滤(常量插值)
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,	
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,	
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8);	//W方向上的寻址模式为CLAMP（钳位寻址模式）

	//lnc
	CD3DX12_STATIC_SAMPLER_DESC lnc(2,	//着色器寄存器
		D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,		//使用线性插值进行缩小和放大；使用最近点采样进行 Mip 级别的采样。
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U方向上的寻址模式为CLAMP（钳位寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V方向上的寻址模式为CLAMP（钳位寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W方向上的寻址模式为CLAMP（钳位寻址模式）

	//llr
	CD3DX12_STATIC_SAMPLER_DESC llr(3,	//着色器寄存器
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,		//使用线性插值进行缩小、放大和 mip 级采样。
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U方向上的寻址模式为CLAMP（重复寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V方向上的寻址模式为CLAMP（重复寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W方向上的寻址模式为CLAMP（重复寻址模式）

	//nnc
	CD3DX12_STATIC_SAMPLER_DESC nnc(4,	//着色器寄存器
		D3D12_FILTER_MIN_MAG_MIP_POINT,			//使用点采样进行缩小、放大和 mip 级别采样。
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U方向上的寻址模式为WRAP（钳位寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V方向上的寻址模式为WRAP（钳位寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W方向上的寻址模式为WRAP（钳位寻址模式）

	//llc
	CD3DX12_STATIC_SAMPLER_DESC llc(5,	//着色器寄存器
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,			//使用线性插值进行缩小、放大和 mip 级采样。
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U方向上的寻址模式为CLAMP（钳位寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V方向上的寻址模式为CLAMP（钳位寻址模式）
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W方向上的寻址模式为CLAMP（钳位寻址模式）



	return { pointWarp, anisotropicClamp, lnc, llr, nnc, llc };
}

D3D12_GPU_DESCRIPTOR_HANDLE RestirGI::GetGPUHandle(UINT offset)
{
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_rtSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
	srvHandle.Offset(offset, m_cbvSrvUavDescriptorSize);
	return srvHandle;
}

matrix RestirGI::getReverseZ(float FovAngleY,
	float AspectRatio,
	float NearZ,
	float FarZ)
{
	float    SinFov;
	float    CosFov;
	DirectX::XMScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

	float Height = CosFov / SinFov;
	float Width = Height / AspectRatio;
	float fRange = NearZ / (NearZ - FarZ);

	XMMATRIX M(Width, 0.0f, 0.0f, 0.0f,
		0.0f, Height, 0.0f, 0.0f,
		0.0f, 0.0f, -fRange, -1.0f,
		0.0f, 0.0f, -fRange * FarZ, 0.0f);
	//open world   far == ∞
	XMMATRIX S(Width, 0.0f, 0.0f, 0.0f,
		0.0f, Height, 0.0f, 0.0f,
		0.0f, 0.0f, 0, -1.0f,
		0.0f, 0.0f, NearZ, 0.0f);

	return S;

}


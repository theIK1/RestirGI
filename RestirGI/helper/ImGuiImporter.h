#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "stdafx.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif


class ImGuiImporter
{
public:
	ImGuiImporter();
	ImGuiImporter(const ImGuiImporter&);
	~ImGuiImporter();

	bool Initialize(HWND hwnd, UINT numFramesInFlight, DXGI_FORMAT rtvFormat,  ID3D12Device5* device, ID3D12GraphicsCommandList4* m_commandList);
	bool BuildGui();
	bool DrawGui();

	void Prepare();
	void Render();
	

private:
	//void IOMap();

	// App Layer Parameter
	HWND m_hwnd = 0;
	UINT m_numFramesInFlight = 0;
	DXGI_FORMAT m_rtvFormat;
	ID3D12DescriptorHeap* m_srvDescHeap = NULL;
	ID3D12Device5* m_device;
	ID3D12GraphicsCommandList4* m_commandList;

	bool show_demo_window = true;
	bool show_another_window = false;
};

#include "stdafx.h"
#include "ImGuiImporter.h"

ImGuiImporter::ImGuiImporter()
{

}

ImGuiImporter::ImGuiImporter(const ImGuiImporter&)
{

}

ImGuiImporter::~ImGuiImporter()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool ImGuiImporter::Initialize(HWND hwnd, UINT numFramesInFlight, DXGI_FORMAT rtvFormat, ID3D12Device5* device, ID3D12GraphicsCommandList4* commandList)
{
    m_hwnd = hwnd;
    m_numFramesInFlight = numFramesInFlight;
    m_rtvFormat = rtvFormat;
    m_device = device;
    m_commandList = commandList;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvDescHeap)) != S_OK)
        return false;


    BuildGui();
    return true;
}

bool ImGuiImporter::BuildGui()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX12_Init(m_device, m_numFramesInFlight,
        m_rtvFormat, m_srvDescHeap,
        m_srvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvDescHeap->GetGPUDescriptorHandleForHeapStart());
	return false;
}

bool ImGuiImporter::DrawGui()
{
    
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }
    // 3. Show another simple window.
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }

    // Rendering
    ImGui::Render();

    // Rendering
    //D3D12_RESOURCE_BARRIER barrier = {};
    //barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    //barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    //barrier.Transition.pResource = mSwapChainBuffer[mCurrBackBuffer].Get();
    //barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    //barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    //barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    //mCommandList->Reset(frameCtx->CommandAllocator, NULL);




    //mCommandList->ResourceBarrier(1, &barrier);
    //mCommandList->OMSetRenderTargets(1, & CurrentBackBufferView(), FALSE, NULL);
    m_commandList->SetDescriptorHeaps(1, &m_srvDescHeap);
    //ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
    //mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    //RenderDrawData(ImGui::GetDrawData());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList);
    return true;
}

void ImGuiImporter::Prepare()
{
    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiImporter::Render()
{
    // Rendering
    ImGui::Render();

    // Rendering
    //D3D12_RESOURCE_BARRIER barrier = {};
    //barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    //barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    //barrier.Transition.pResource = mSwapChainBuffer[mCurrBackBuffer].Get();
    //barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    //barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    //barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    //mCommandList->Reset(frameCtx->CommandAllocator, NULL);




    //mCommandList->ResourceBarrier(1, &barrier);
    //mCommandList->OMSetRenderTargets(1, & CurrentBackBufferView(), FALSE, NULL);
    m_commandList->SetDescriptorHeaps(1, &m_srvDescHeap);
    //ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
    //mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    //RenderDrawData(ImGui::GetDrawData());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList);
}



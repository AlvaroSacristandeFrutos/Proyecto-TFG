// ============================================================================
// MAIN.CPP - TOPJTAG CLONE UI (CON DOCKING & VIEWPORTS)
// ============================================================================

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>

// AJUSTA TU RUTA
#include "controller/ScanController.h" 

// Variables Globales DirectX
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// MAIN
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"JtagScanner", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"JTAG Scanner (Pro Docking)", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // ------------------------------------------------------------------------
    // ACTIVAR DOCKING Y VIEWPORTS (La clave de todo)
    // ------------------------------------------------------------------------
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Navegación con teclado
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // ACTIVAR DOCKING
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // ACTIVAR VENTANAS FLOTANTES (Multi-monitor)

    // Estilo Visual
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // --- ESTADO ---
    JTAG::ScanController controller;
    char bsdlPathBuf[256] = "STM32F4.bsdl";
    char pinSearchBuf[128] = "";
    bool scanRunning = false;

    // Estado de Ventanas
    bool showPins = true;
    bool showWaveform = true;
    bool showCircuit = true;
    bool showToolbar = true;

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // --------------------------------------------------------------------
        // CREAR EL ESPACIO DE DOCKING (DOCKSPACE)
        // --------------------------------------------------------------------
        // Esto crea un contenedor invisible que ocupa toda la ventana principal
        // y permite que otras ventanas se "peguen" a él.
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        // ============================================================
        // 1. MENÚ PRINCIPAL (Ahora vive arriba del DockSpace)
        // ============================================================
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open BSDL...")) controller.loadDeviceModel(bsdlPathBuf);
                if (ImGui::MenuItem("Exit", "Alt+F4")) done = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Toolbar", nullptr, &showToolbar);
                ImGui::Separator();
                ImGui::MenuItem("Pins List", nullptr, &showPins);
                ImGui::MenuItem("Circuit View", nullptr, &showCircuit);
                ImGui::MenuItem("Waveform", nullptr, &showWaveform);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Scan")) {
                if (ImGui::MenuItem("Initialize Chain")) controller.initializeDevice();
                if (ImGui::MenuItem("Run Test", "F5", &scanRunning)) {}
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ============================================================
        // 2. TOOLBAR (Ahora es una ventana normal que puedes arrastrar)
        // ============================================================
        if (showToolbar) {
            // Le quitamos la barra de título para que parezca toolbar, pero si quieres moverla
            // puedes quitar ImGuiWindowFlags_NoTitleBar temporalmente o usar el grip.
            ImGui::Begin("Toolbar", &showToolbar, ImGuiWindowFlags_NoScrollbar);

            if (ImGui::Button("[Open]")) controller.loadDeviceModel("");
            ImGui::SameLine();
            ImGui::TextDisabled("|"); ImGui::SameLine();

            // Botones de acción
            if (ImGui::Button("Connect J-Link")) controller.connectAdapter(JTAG::AdapterType::JLINK);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, scanRunning ? ImVec4(0.8f, 0.2f, 0.2f, 1.f) : ImVec4(0.4f, 0.8f, 0.4f, 1.f));
            if (ImGui::Button(scanRunning ? " STOP " : " RUN ")) scanRunning = !scanRunning;
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::Text("IDCODE: 0x%08X", controller.getIDCODE());

            ImGui::End();
        }

        // ============================================================
        // 3. VENTANAS ACOPLABLES (PINES, CIRCUITO, ONDA)
        // ============================================================

        // Ventana de Pines
        if (showPins) {
            ImGui::Begin("Pins List", &showPins); // Sin flags de posición fija!

            ImGui::InputText("Filter", pinSearchBuf, IM_ARRAYSIZE(pinSearchBuf));
            ImGui::Separator();

            if (ImGui::BeginTable("PinsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Port");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Type");
                ImGui::TableHeadersRow();

                auto pins = controller.getPinList();
                for (const auto& pinName : pins) {
                    if (pinSearchBuf[0] != 0 && pinName.find(pinSearchBuf) == std::string::npos) continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%s", pinName.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("A");
                    ImGui::TableSetColumnIndex(2);
                    auto st = controller.getPin(pinName);
                    if (st.has_value() && st.value() == JTAG::PinLevel::HIGH) ImGui::TextColored(ImVec4(0, 0.8f, 0, 1), "1");
                    else ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "0");
                    ImGui::TableSetColumnIndex(3); ImGui::Text("IO");
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        // Ventana de Circuito
        if (showCircuit) {
            ImGui::Begin("Circuit View", &showCircuit);
            // Dibujo simple
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ImVec2(p.x + 50, p.y + 50), ImVec2(p.x + 200, p.y + 200), IM_COL32(230, 230, 230, 255));
            dl->AddRect(ImVec2(p.x + 50, p.y + 50), ImVec2(p.x + 200, p.y + 200), IM_COL32(0, 0, 0, 255));
            dl->AddText(ImVec2(p.x + 90, p.y + 110), IM_COL32(0, 0, 0, 255), "CHIP VIEW");
            ImGui::End();
        }

        // Ventana de Ondas
        if (showWaveform) {
            ImGui::Begin("Waveform", &showWaveform);
            ImGui::Text("Logic Analyzer Simulation");
            ImGui::Separator();
            float vals[] = { 0,0,1,1,0,0,1,1,0,0,0,0,1,1,1,1 };
            ImGui::PlotLines("TCK", vals, 16, 0, nullptr, -0.1f, 1.1f, ImVec2(-1, 40));
            ImGui::PlotLines("TDI", vals, 16, 0, nullptr, -0.1f, 1.1f, ImVec2(-1, 40));
            ImGui::End();
        }

        // Render
        ImGui::Render();
        const float clear_color[4] = { 0.80f, 0.80f, 0.80f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows (Viewports)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// BOILERPLATE (Igual que siempre)
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT createDeviceFlags = 0; D3D_FEATURE_LEVEL featureLevel; const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false; CreateRenderTarget(); return true;
}
void CleanupDeviceD3D() { CleanupRenderTarget(); if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; } if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; } if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; } }
void CreateRenderTarget() { ID3D11Texture2D* pBackBuffer; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)); g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView); pBackBuffer->Release(); }
void CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; } }
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) { if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true; switch (msg) { case WM_SIZE: if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) { CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget(); } return 0; case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break; case WM_DESTROY: ::PostQuitMessage(0); return 0; } return ::DefWindowProcW(hWnd, msg, wParam, lParam); }
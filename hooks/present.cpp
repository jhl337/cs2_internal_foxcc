#include "pch.h"

#define STB_IMAGE_IMPLEMENTATION
#include "sdk/stb_image.h"
#include "sdk/unlink.h"
#include "sdk/hooks.h"
#include "hooks/radar.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern HMODULE g_hModule;

namespace Hooks {

    // Typedefs
    typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    typedef LRESULT(__stdcall* WndProc_t)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Original functions
    Present_t oPresent = nullptr;
    ResizeBuffers_t oResizeBuffers = nullptr;
    WndProc_t oWndProc = nullptr;

    // D3D11 objects
    ID3D11Device* g_pDevice = nullptr;
    ID3D11DeviceContext* g_pContext = nullptr;
    ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

    // Window
    HWND g_hWnd = nullptr;

    // Client
    uintptr_t clientBase = NULL;

    // State
    bool g_bInitialized = false;
    bool g_bMenuOpen = true;

    // Feature toggles
    bool g_bEspEnabled = false;
    bool g_bEspBoxes = true;
    bool g_bEspHealth = true;
    bool g_bEspNames = true;
    bool g_bEspTeamCheck = true;
    bool g_bEspFovCircle = true;
	bool g_bSpectatorList = false;
	bool g_bRadar = false;
	bool g_bRadarTeamCheck = true;
    float g_fRadarSize = 220.f;
	float g_fRadarProportion = 3300.f;
    
	// Triggerbot settings
    bool g_bTriggerbotEnabled = false;
	int g_nTriggerbotDelay = 30;
	int g_nTriggerbotDuration = 50;
	bool g_bTriggerbotTeamCheck = true;

    // Aimbot settings
    bool g_bAimbotEnabled = false;
    float g_fAimbotFov = 5.0f;
    float g_fAimbotSmooth = 5.0f;
    bool g_bAimbotTeamCheck = true;
    bool g_bAimbotFlashCheck = true;
    bool g_bAimbotVisCheck = true;
    bool g_bAimbotRcs = true;
    int g_nAimbotBone = 6; // Head

	// Blatant settings
	bool g_bGlowEnabled = false;
	bool g_bGlowRainbow = false;
	bool g_bGlowTeamCheck = true;
	bool g_bGlowAroundCross = false;
	bool g_bNoFlashEnabled = false;
	float g_fNoFlashChangeToValue = 0.0f;

    // Key bindings
    int g_nAimbotKey = VK_XBUTTON2;
    int g_nTriggerbotKey = VK_XBUTTON1;
    bool g_bWaitingForAimKey = false;
    bool g_bWaitingForTriggerKey = false;

    // Thread handles
    HANDLE g_hAimbotThread = nullptr;
    HANDLE g_hTriggerbotThread = nullptr;
    HANDLE g_hBlatantThread = nullptr;
    HANDLE g_hInjectThread = nullptr;
    bool g_bRunning = true;

    // Easter Egg
	int PutinHasBeenFuckedTimes = 0;

    // Forward declarations
    void RenderMenu();
    void RenderESP();
    void RunAimbot();
    void RunTriggerbot();
    void RunBlatant();
    bool CreateResources(ID3D11Device* pDevice);
    DWORD WINAPI AimbotThread(LPVOID lpParam);
    DWORD WINAPI TriggerbotThread(LPVOID lpParam);
    DWORD WINAPI BlatantThread(LPVOID lpParam);
    DWORD WINAPI InjectThread(LPVOID lpParam);


    std::map<int, std::string> keyNames = {
        {0x01, "LMouse"}, {0x02, "RMouse"}, {0x04, "MidMouse"},
        {0x05, "X1"}, {0x06, "X2"},
        {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"}, {VK_LMENU, "LAlt"},
        {VK_SPACE, "Space"}, {VK_CAPITAL, "Caps"}, {VK_HOME, "HOME"},
        {VK_LBUTTON, "LMouse"}, {VK_RBUTTON, "RMouse"}
    };

    std::string GetKeyName(int key) {
        if (keyNames.find(key) != keyNames.end()) return keyNames[key];
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, (char)key);
        return std::to_string(key);
    }

    bool IsPressed(int key) {
        SPOOF_FUNC;
        return (SPOOF_CALL(GetAsyncKeyState)(key) & 0x8000) != 0;
    }

    // WndProc hook
    LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        SPOOF_FUNC;
        if (msg == WM_KEYDOWN && wParam == VK_HOME) {
            g_bMenuOpen = !g_bMenuOpen;
            return 0;
        }

        if (g_bMenuOpen) {
            if (SPOOF_CALL(ImGui_ImplWin32_WndProcHandler)(hWnd, msg, wParam, lParam)) {
                return 0;
            }
        }

        return SPOOF_CALL(CallWindowProcA)(oWndProc, hWnd, msg, wParam, lParam);
    }

    // Present hook
    HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        SPOOF_FUNC;
        if (!g_bInitialized) {
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pDevice))) {
                g_pDevice->GetImmediateContext(&g_pContext);
                DXGI_SWAP_CHAIN_DESC desc;
                pSwapChain->GetDesc(&desc);
                g_hWnd = desc.OutputWindow;

                ID3D11Texture2D* pBackBuffer = nullptr;
                pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
                if (pBackBuffer) {
                    g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
                    pBackBuffer->Release();
                }
                oWndProc = (WndProc_t)SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
                ImGui::CreateContext();
				
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                auto& style = ImGui::GetStyle();
                style.WindowRounding = 8.0f;
                style.ChildRounding = 6.0f;
                style.FrameRounding = 4.0f;
                style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);
                style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
                style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
                style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
                style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.21f, 1.00f);
                style.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
                style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
				
                ImGui_ImplWin32_Init(g_hWnd);
                ImGui_ImplDX11_Init(g_pDevice, g_pContext);
                CreateResources(g_pDevice);
                g_bInitialized = true;
            }
        }

        if (g_bInitialized) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            if (g_bMenuOpen) {
                RenderMenu();
            }
            if (g_bEspEnabled) {
                RenderESP();
            }
            ImGui::Render();
            g_pContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        if (g_pRenderTargetView) {
            g_pRenderTargetView->Release();
            g_pRenderTargetView = nullptr;
        }
        HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        ID3D11Texture2D* pBackBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        if (pBackBuffer) {
            g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
            pBackBuffer->Release();
        }
        return hr;
    }

    HANDLE StartBypassThread(LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter) {
        SPOOF_FUNC;
        HANDLE hThread = (HANDLE)SPOOF_CALL(_beginthreadex)(nullptr, 0, (_beginthreadex_proc_type)lpStartAddress, lpParameter, CREATE_SUSPENDED, nullptr);
        if (hThread && hThread != INVALID_HANDLE_VALUE) {
            SPOOF_CALL(ResumeThread)(hThread);
            return hThread;
        }
        return NULL;
    }

    void Initialize() {
        SPOOF_FUNC;
        clientBase = (uintptr_t)SPOOF_CALL(GetModuleHandleA)("client.dll");
        if (!clientBase) return;
        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "DirectX11", nullptr };
        RegisterClassExA(&wc);
        HWND hWnd = CreateWindowA("DirectX11", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        IDXGISwapChain* pSwapChain = nullptr;
        D3D_FEATURE_LEVEL featureLevel;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &pSwapChain, &pDevice, &featureLevel, &pContext))) {
            DestroyWindow(hWnd);
            UnregisterClassA("DirectX11", wc.hInstance);
            return;
        }
        void** pVTable = *(void***)pSwapChain;
        void* pPresent = pVTable[8];
        void* pResizeBuffers = pVTable[13];
        pContext->Release();
        pDevice->Release();
        pSwapChain->Release();
        DestroyWindow(hWnd);
        UnregisterClassA("DirectX11", wc.hInstance);
        MH_Initialize();
        MH_CreateHook(pPresent, &hkPresent, (void**)&oPresent);
        MH_EnableHook(pPresent);
        MH_CreateHook(pResizeBuffers, &hkResizeBuffers, (void**)&oResizeBuffers);
        MH_EnableHook(pResizeBuffers);
        EnableHooks();
        g_hInjectThread = StartBypassThread((LPTHREAD_START_ROUTINE)InjectThread, nullptr);
    }

    void Shutdown() {
        g_bRunning = false;
        if (g_hAimbotThread) {
            WaitForSingleObject(g_hAimbotThread, 1000);
            CloseHandle(g_hAimbotThread);
            g_hAimbotThread = nullptr;
        }
        if (g_hTriggerbotThread) {
            WaitForSingleObject(g_hTriggerbotThread, 1000);
            CloseHandle(g_hTriggerbotThread);
            g_hTriggerbotThread = nullptr;
        }
        if (g_hBlatantThread) {
            WaitForSingleObject(g_hBlatantThread, 1000);
            CloseHandle(g_hBlatantThread);
            g_hBlatantThread = nullptr;
        }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (oWndProc) {
            SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        }
        if (g_bInitialized) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        if (g_pRenderTargetView) g_pRenderTargetView->Release();
        if (g_pContext) g_pContext->Release();
        if (g_pDevice) g_pDevice->Release();
    }

    ID3D11ShaderResourceView* g_pLogoTexture = nullptr;
    bool g_bGlobalLoaded = false;
    int g_iLoadStep = 1;

    bool CreateResources(ID3D11Device* pDevice) {
        SPOOF_FUNC;
        int width, height, channels;
        unsigned char* data = stbi_load_from_memory(yh_png_data, yh_png_size, &width, &height, &channels, 4);
        if (!data) return false;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subRes = { data, (UINT)(width * 4), 0 };
        ID3D11Texture2D* pTex = nullptr;
        pDevice->CreateTexture2D(&desc, &subRes, &pTex);
        if (pTex) {
            pDevice->CreateShaderResourceView(pTex, nullptr, &g_pLogoTexture);
            pTex->Release();
        }
        stbi_image_free(data);
        return g_pLogoTexture != nullptr;
    }

    ImColor GetRainbowColor(float offset) {
        static double time = 0.0f;
        time += 0.00001;
        float r = (float)(std::sin(time * 2.0f * PI + offset) + 1.0f) * 0.5f;
        float g = (float)(std::sin(time * 2.0f * PI + offset + 2.0f * PI / 3.0f) + 1.0f) * 0.5f;
        float b = (float)(std::sin(time * 2.0f * PI + offset + 4.0f * PI / 3.0f) + 1.0f) * 0.5f;

        return ImColor(r, g, b, 1.0f);
    }

    void RenderMenu() {
        SPOOF_FUNC;
        ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));

        if (!ImGui::Begin("Comp", &g_bMenuOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::End();
            return;
        }

        float windowWidth = ImGui::GetWindowWidth();
        float windowHeight = ImGui::GetWindowHeight();

        if (!g_bGlobalLoaded) {
            if (g_pLogoTexture) {
                ImGui::SetCursorPos(ImVec2((windowWidth - 128) * 0.5f, 80));
                ImGui::Image((void*)g_pLogoTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1));
            }

            const char* text = "FoxCheat";
            float textWidth = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPos(ImVec2((windowWidth - textWidth) * 0.5f, 220));

            for (int i = 0; i < strlen(text); i++)
            {
                ImColor col = GetRainbowColor(i * 0.5f);
                ImGui::TextColored(col, "%c", text[i]);
                if (i < strlen(text) - 1)
                    ImGui::SameLine(0.0f, 0.0f);
            }

            ImGui::SetCursorPos(ImVec2(0, windowHeight - 85));
            float statusWidth = ImGui::CalcTextSize("Injecting...").x;
            ImGui::SetCursorPosX((windowWidth - statusWidth) * 0.5f);
            ImGui::TextDisabled("Injecting...");
            ImGui::SetCursorPos(ImVec2(50, windowHeight - 60));
            float progress = (float)g_iLoadStep / 4.0f;
            char progressBuf[16];
            sprintf_s(progressBuf, "%d / 4", g_iLoadStep);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.26f, 0.59f, 0.98f, 1.0f));
            ImGui::ProgressBar(progress, ImVec2(windowWidth - 100, 20), progressBuf);
            ImGui::PopStyleColor();

        }
        else {
            static int tab = 0;
            ImGui::BeginChild("LeftSection", ImVec2(120, 0), false);
            {
                if (g_pLogoTexture) {
                    ImGui::SetCursorPosX((120 - 64) * 0.5f);
                    ImGui::Image((void*)g_pLogoTexture, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
                }

                const char* text = "FoxCheat";
                float textWidth = ImGui::CalcTextSize(text).x;
                ImGui::SetCursorPosX((120 - textWidth) * 0.5f);

                for (int i = 0; i < strlen(text); i++)
                {
                    ImColor col = GetRainbowColor(i * 0.5f);
                    ImGui::TextColored(col, "%c", text[i]);
                    if (i < strlen(text) - 1)
                        ImGui::SameLine(0.0f, 0.0f);
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::BeginChild("Sidebar", ImVec2(120, 0), true);
                {
                    if (ImGui::Selectable("VISUALS", tab == 0)) tab = 0;
                    if (ImGui::Selectable("LEGITBOT", tab == 1)) tab = 1;
                    if (ImGui::Selectable("BLATANT", tab == 2)) tab = 2;
                    if (ImGui::Selectable("MISC", tab == 3)) tab = 3;
                    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 25);
                    ImGui::TextDisabled("v1.0.2 (Stable)");
                }
                ImGui::EndChild();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::SameLine();
            ImGui::BeginGroup();
            const char* titles[] = { "VISUALS", "LEGITBOT", "BLATANT", "MISC" };
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), titles[tab]);
            ImGui::Separator();
            ImGui::Spacing();

            if (tab == 0) {
                ImGui::Checkbox("Master Switch", &g_bEspEnabled);
                ImGui::BeginChild("ESP_Sub", ImVec2(0, 60), true);
                ImGui::Columns(2, NULL, false);
                ImGui::Checkbox("Bounding Box", &g_bEspBoxes);
                ImGui::Checkbox("Health Bar", &g_bEspHealth);
                ImGui::NextColumn();
                ImGui::Checkbox("Player Names", &g_bEspNames);
                ImGui::Checkbox("Team Check", &g_bEspTeamCheck);
                ImGui::Columns(1);
                ImGui::EndChild();

                ImGui::Checkbox("Radar Master", &g_bRadar);
                ImGui::BeginChild("Radar_Sub", ImVec2(0, 82), true);
                
                ImGui::Checkbox("Team Check", &g_bRadarTeamCheck);
                ImGui::SliderFloat("Size", &g_fRadarSize, 150, 500, "%.0f");
                ImGui::SliderFloat("Proportion", &g_fRadarProportion, 2400.f, 5000.f, "%.0f");
                ImGui::EndChild();
            }
            else if (tab == 1) {
                ImGui::Checkbox("Aimbot Master", &g_bAimbotEnabled);
                ImGui::BeginChild("Aim_Sub", ImVec2(0, 223), true);
                if (g_bWaitingForAimKey) {
                    ImGui::Button("Press any key...", ImVec2(150, 0));
                    for (int i = 1; i < 256; i++) {
                        if (GetAsyncKeyState(i) & 0x8000) {
                            g_nAimbotKey = i;
                            g_bWaitingForAimKey = false;
                            break;
                        }
                    }
                }
                else {
                    std::string btnLabel = "Aim Key: " + GetKeyName(g_nAimbotKey);
                    if (ImGui::Button(btnLabel.c_str(), ImVec2(150, 0))) g_bWaitingForAimKey = true;
                }

                ImGui::SliderFloat("FOV", &g_fAimbotFov, 1.0f, 30.0f, "%.1f deg");
                ImGui::SliderFloat("Smoothing", &g_fAimbotSmooth, 1.0f, 100.0f, "%.1f");
                const char* bones[] = { "Head", "Neck", "Chest" };
                static int boneIdx = 0;
                ImGui::SetNextItemWidth(150);
                if (ImGui::Combo("Target Bone", &boneIdx, bones, IM_ARRAYSIZE(bones))) {
                    int boneMap[] = { 6, 5, 4 };
                    g_nAimbotBone = boneMap[boneIdx];
                }

                ImGui::Separator();
                ImGui::Checkbox("FOV Circle", &g_bEspFovCircle);
                ImGui::Checkbox("Team Check", &g_bAimbotTeamCheck);
                ImGui::Checkbox("Flash Check", &g_bAimbotFlashCheck);
                ImGui::Checkbox("Visible Only", &g_bAimbotVisCheck);
                ImGui::Checkbox("Recoil Control", &g_bAimbotRcs);
                ImGui::EndChild();

                ImGui::Checkbox("Triggerbot Master", &g_bTriggerbotEnabled);
                ImGui::BeginChild("Trigger_Sub", ImVec2(0, 82), true);
                if (g_bWaitingForTriggerKey) {
                    ImGui::Button("Press any key...##trig", ImVec2(150, 0));
                    for (int i = 1; i < 256; i++) {
                        if (GetAsyncKeyState(i) & 0x8000) {
                            g_nTriggerbotKey = i;
                            g_bWaitingForTriggerKey = false;
                            break;
                        }
                    }
                }
                else {
                    std::string btnLabel = "Trigger Key: " + GetKeyName(g_nTriggerbotKey);
                    if (ImGui::Button(btnLabel.c_str(), ImVec2(150, 0))) g_bWaitingForTriggerKey = true;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Team Check", &g_bTriggerbotTeamCheck);
                ImGui::SliderInt("Shot Delay (ms)", &g_nTriggerbotDelay, 0, 100);
                ImGui::SliderInt("Shot Duration (ms)", &g_nTriggerbotDuration, 0, 100);
                ImGui::EndChild();
            }
            else if (tab == 2) {
                ImGui::Checkbox("Glow Master", &g_bGlowEnabled);
                ImGui::BeginChild("Glow_Sub", ImVec2(0, 60), true);
                ImGui::Columns(2, NULL, false);
                ImGui::Checkbox("Rainbow", &g_bGlowRainbow);
                ImGui::Checkbox("Team Check", &g_bGlowTeamCheck);
                ImGui::NextColumn();
                ImGui::Checkbox("Around Cross", &g_bGlowAroundCross);
                ImGui::Columns(2);
                ImGui::EndChild();
                ImGui::Checkbox("No Flash Master", &g_bNoFlashEnabled);
                ImGui::BeginChild("NoFlash_Sub", ImVec2(0, 35), true);
                ImGui::SliderFloat("Flash Duration", &g_fNoFlashChangeToValue, 0.0f, 5.0f, "%.1f");
                ImGui::EndChild();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "[!] Using features in this tab may lead to a game ban!");
            }
            else if (tab == 3) {
                ImGui::Checkbox("Spectator List", &g_bSpectatorList);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Fuck Putin", ImVec2(120, 30))) {
                    PutinHasBeenFuckedTimes++;
                }
                ImGui::Spacing();
                ImGui::Text("Hotkeys:");
                ImGui::BulletText("HOME - Toggle Menu");
                ImGui::BulletText("END - Unload");
                ImGui::Spacing();
                ImGui::Text("Putin has been fucked %d times.", PutinHasBeenFuckedTimes);
            }

            ImGui::EndGroup();
        }
        ImGui::End();
    }

    ImColor rainbow() {
        static double time = 0.0f;
        time += 0.1 / 10000;

        float r = (float)(std::sin(time * 2.0f * PI) + 1.0f) * 0.5f;
        float g = (float)(std::sin(time * 2.0f * PI + 2.0f * PI / 3.0f) + 1.0f) * 0.5f;
        float b = (float)(std::sin(time * 2.0f * PI + 4.0f * PI / 3.0f) + 1.0f) * 0.5f;

        return ImColor(r, g, b, 200.f);
    }

    void RunBlatant() {
        SPOOF_FUNC;
        uintptr_t entityList = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwEntityList);
        if (!entityList) return;
        uintptr_t localPawn = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPawn) return;
        int localTeam = *(uint8_t*)(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);

        for (int i = 1; i <= 64; i++) {
            uintptr_t listEntry = *(uintptr_t*)(entityList + 0x10 + 8 * (i >> 9));
            if (!listEntry) continue;

            uintptr_t controller = *(uintptr_t*)(listEntry + 0x70 * (i & 0x1FF));
            if (!controller) continue;

            uint32_t pawnHandle = *(uint32_t*)(controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn); // m_hPawn
            if (!pawnHandle) continue;

            uintptr_t pawnEntry = *(uintptr_t*)(entityList + 0x10 + 8 * ((pawnHandle & 0x7FFF) >> 9));
            if (!pawnEntry) continue;

            uintptr_t pawn = *(uintptr_t*)(pawnEntry + 0x70 * (pawnHandle & 0x1FF));
            if (!pawn || pawn == localPawn) continue;

            int health = *(int*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
            int team = *(uint8_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
            uint8_t lifeState = *(uint8_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState);
            if (health <= 0 || lifeState != 0) continue;

            if (g_bGlowEnabled) {
                DWORD color;
                if (g_bGlowRainbow) {
                    color = rainbow();
                }
                else
                {
                    color = ImColor(200.f, 200.f, 200.f, 120.f);
                }
                if (g_bGlowAroundCross) {
                    *(DWORD*)(pawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_Glow + cs2_dumper::schemas::client_dll::CGlowProperty::m_iGlowType) = 2;
                }
                if (g_bGlowTeamCheck) {
                    if (team != localTeam)
                    {
                        *(int*)(pawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_Glow + cs2_dumper::schemas::client_dll::CGlowProperty::m_bGlowing) = 1;
                        *(DWORD*)(pawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_Glow + cs2_dumper::schemas::client_dll::CGlowProperty::m_glowColorOverride) = color;
                    }
                }
                else {
                    *(int*)(pawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_Glow + cs2_dumper::schemas::client_dll::CGlowProperty::m_bGlowing) = 1;
                    *(DWORD*)(pawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_Glow + cs2_dumper::schemas::client_dll::CGlowProperty::m_glowColorOverride) = color;
                }
            }
            if (g_bNoFlashEnabled)
            {
                float flashDuration = *(float*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration);
                if (flashDuration > g_fNoFlashChangeToValue) {
                    *(float*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration) = g_fNoFlashChangeToValue;
                }
            }
        }
    }

    void RadarSetting(Base_Radar& Radar)
    {
        ImGui::SetNextWindowBgAlpha(0.1f);
        ImGui::Begin("Radar", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
        ImGui::SetWindowSize({ g_fRadarSize * 2,g_fRadarSize * 2 });
        ImGui::SetWindowPos({ 0.f,0.f });
        Radar.SetDrawList(ImGui::GetWindowDrawList());
        Radar.SetPos({ ImGui::GetWindowPos().x + g_fRadarSize, ImGui::GetWindowPos().y + g_fRadarSize });
        Radar.SetProportion(g_fRadarProportion);
        Radar.SetRange(g_fRadarSize);
        Radar.SetSize(g_fRadarSize * 2);
        Radar.SetCrossColor(ImColor(220, 220, 220, 255));
        Radar.ArcArrowSize *= 1.f;
        Radar.ArrowSize *= 1.f;
        Radar.CircleSize *= 1.f;
        Radar.ShowCrossLine = false;
        Radar.ShowRadar = true;
    }

    Base_Radar Radar;

    inline bool IsSafePtr(uintptr_t adr) {
        return (adr >= 0x10000 && adr < 0x00007FFFFFFFFFFF);
    }

    void RenderESP() {
        SPOOF_FUNC;
        uintptr_t entityList = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwEntityList);
        if (!IsSafePtr(entityList)) return;
        uintptr_t localPawn = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPawn) return;
        int localTeam = *(uint8_t*)(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
        view_matrix_t viewMatrix;
        SPOOF_CALL(memcpy)(&viewMatrix, (void*)(clientBase + cs2_dumper::offsets::client_dll::dwViewMatrix), sizeof(view_matrix_t));
        ImGuiIO& io = ImGui::GetIO();
        int screenWidth = (int)io.DisplaySize.x;
        int screenHeight = (int)io.DisplaySize.y;
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        std::vector<std::string> spectators;

		
        if (g_bRadar) {
            RadarSetting(Radar);
        }
		
        for (int i = 1; i <= 64; i++) {
			
            uintptr_t listEntry = *(uintptr_t*)(entityList + 0x10 + 8 * (i >> 9));
            if (!listEntry) continue;

            uintptr_t controller = *(uintptr_t*)(listEntry + 0x70 * (i & 0x1FF));
            if (!controller) continue;

            uint32_t pawnHandle = *(uint32_t*)(controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn);
            if (!pawnHandle) continue;

            uintptr_t pawnEntry = *(uintptr_t*)(entityList + 0x10 + 8 * ((pawnHandle & 0x7FFF) >> 9));
            if (!pawnEntry) continue;

            uintptr_t pawn = *(uintptr_t*)(pawnEntry + 0x70 * (pawnHandle & 0x1FF));
            if (!pawn || pawn == localPawn) continue;

            int health = *(int*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
            int team = *(uint8_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
            uint8_t lifeState = *(uint8_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState);

            if (g_bSpectatorList && pawn != localPawn) {
                uintptr_t obsServices = *(uintptr_t*)(pawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_pObserverServices);
                if (obsServices) {
                    uint32_t obsTargetHandle = *(uint32_t*)(obsServices + cs2_dumper::schemas::client_dll::CPlayer_ObserverServices::m_hObserverTarget);
                    // if (!obsTargetHandle) continue;
                    uintptr_t targetPawnEntry = *(uintptr_t*)(entityList + 0x10 + 8 * ((obsTargetHandle & 0x7FFF) >> 9));
                    if (targetPawnEntry) {
                        uintptr_t targetPawn = *(uintptr_t*)(targetPawnEntry + 0x70 * (obsTargetHandle & 0x1FF));
                        if (targetPawn == localPawn) {
                            const char* specName = (const char*)(controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName);
                            if (specName && specName[0]) {
                                spectators.push_back(specName);
                            }
                        }
                    }
                }
            }

            if (health <= 0 || lifeState != 0) continue;

            Vector3 localpos = *(Vector3*)(localPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
            Vector3 enemypos = *(Vector3*)(pawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);

            if (g_bRadar) {
                ImColor color;
                if (health >= 80) {
                    color = ImColor(14, 222, 40, 200);
                }
                else if (health < 80 && health >= 30) {
                    color = ImColor(235, 248, 78, 200);
                }
                else {
                    color = ImColor(237, 85, 106, 200);
                }
                if (g_bRadarTeamCheck && localTeam != team)
                {
                    Vector2 localviewangle = *(Vector2*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
                    Vector2 enemyviewangle = *(Vector2*)(pawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
                    Radar.AddPoint(localpos, localviewangle.y, enemypos, color, 2, enemyviewangle.y);
                }
                else if (!g_bRadarTeamCheck)
                {
                    Vector2 localviewangle = *(Vector2*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
                    Vector2 enemyviewangle = *(Vector2*)(pawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
                    Radar.AddPoint(localpos, localviewangle.y, enemypos, color, 2, enemyviewangle.y);
				}
            }

            if (g_bEspTeamCheck && localTeam == team) continue;

            uintptr_t gameSceneNode = *(uintptr_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode);
            if (!gameSceneNode) continue;

            Vector3 origin = *(Vector3*)(gameSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin);
            
            Vector2 screenPos;
            Vector2 headScreenPos;
            Vector3 headPos = origin;
            headPos.z += 72.0f;

            if (!WorldToScreen(origin, screenPos, viewMatrix, screenWidth, screenHeight)) continue;
            if (!WorldToScreen(headPos, headScreenPos, viewMatrix, screenWidth, screenHeight)) continue;

            float boxHeight = screenPos.y - headScreenPos.y;
            float boxWidth = boxHeight * 0.5f;

            ImU32 color = (team == 2) ? IM_COL32(255, 100, 100, 255) : IM_COL32(100, 100, 255, 255);

            float rounding = 5.0f;
            float outlineThickness = 3.0f;
            float mainThickness = 1.0f;
            ImU32 outlineColor = IM_COL32(0, 0, 0, 180);

            if (g_bEspBoxes) {
                ImVec2 min = ImVec2(headScreenPos.x - boxWidth / 2, headScreenPos.y);
                ImVec2 max = ImVec2(headScreenPos.x + boxWidth / 2, screenPos.y);
                drawList->AddRect(min, max, outlineColor, rounding, ImDrawFlags_RoundCornersAll, outlineThickness);
                drawList->AddRect(min, max, color, rounding, ImDrawFlags_RoundCornersAll, mainThickness);
            }

            if (g_bEspHealth) {
                float healthPercent = health / 100.0f;
                ImU32 healthColor = IM_COL32(255 * (1.0f - healthPercent), 255 * healthPercent, 0, 255);
                ImVec2 barMin = ImVec2(headScreenPos.x - boxWidth / 2 - 8, headScreenPos.y);
                ImVec2 barMax = ImVec2(headScreenPos.x - boxWidth / 2 - 4, screenPos.y);
                drawList->AddRectFilled(barMin, barMax, IM_COL32(10, 10, 10, 180), 2.0f);
                float barHeight = screenPos.y - headScreenPos.y;
                ImVec2 currentHealthMin = ImVec2(barMin.x, screenPos.y - (barHeight * healthPercent));
                drawList->AddRectFilled(currentHealthMin, barMax, healthColor, 2.0f, ImDrawFlags_RoundCornersBottom);
                drawList->AddRect(barMin, barMax, outlineColor, 2.0f, ImDrawFlags_RoundCornersAll, 1.0f);
            }

            if (g_bEspNames) {
                const char* name = (const char*)(controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName);
                if (name && name[0]) {
                    ImVec2 textSize = ImGui::CalcTextSize(name);
                    ImVec2 textPos = ImVec2(headScreenPos.x - textSize.x / 2, headScreenPos.y - textSize.y - 5);
                    drawList->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, 220), name);
                    drawList->AddText(textPos, IM_COL32(66, 150, 250, 255), name);
                }
            }
        }
        if (g_bRadar)
        {
            Radar.Render();
            ImGui::End();
        }

        if (g_bEspFovCircle && g_bAimbotEnabled) {
            drawList->AddCircle(
                ImVec2(screenWidth / 2.0f, screenHeight / 2.0f),
                g_fAimbotFov * 10.0f,
                IM_COL32(255, 255, 255, 100),
                64, 1.5f
            );
        }

        if (g_bSpectatorList && !spectators.empty()) {
            ImGui::SetNextWindowSize(ImVec2(200, 0));
            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;

            if (ImGui::Begin("Spectators", NULL, windowFlags)) {
                for (const auto& name : spectators) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[!] %s", name.c_str());
                }
            }
            ImGui::End();
        }
    }

    Vector3 GetBonePosition(uintptr_t pawn, int boneId) {
        uintptr_t gameSceneNode = *(uintptr_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode);
        if (!gameSceneNode) return { 0, 0, 0 };
        
        uintptr_t boneArray = *(uintptr_t*)(gameSceneNode + 0x1E0);
        if (!boneArray) boneArray = *(uintptr_t*)(gameSceneNode + 0x1F0);
        if (!boneArray) return { 0, 0, 0 };
        
        return *(Vector3*)(boneArray + boneId * 32);
    }

    static uintptr_t g_lockedPawn = 0;

    void RunAimbot() {
        SPOOF_FUNC;
        if (!IsPressed(g_nAimbotKey)) {
            g_lockedPawn = 0;
            return;
        }

        uintptr_t localController = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController);
        if (!localController) return;

        uintptr_t entityList = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwEntityList);
        if (!entityList) return;

        uintptr_t localPawn = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPawn) return;

        int localIndex = -1;
        for (int i = 0; i < 64; i++) {
            uintptr_t entry = *(uintptr_t*)(entityList + 0x10 + 8 * (i >> 9));
            if (!entry) continue;
            uintptr_t ctrl = *(uintptr_t*)(entry + 0x70 * (i & 0x1FF));
            if (ctrl == localController) {
                localIndex = i;
                break;
            }
        }
        if (localIndex == -1) return;

        uintptr_t localSceneNode = *(uintptr_t*)(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode);
        if (!localSceneNode) return;
        
        Vector3 localOrigin = *(Vector3*)(localSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin);
        Vector3 viewOffset = *(Vector3*)(localPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
        if (viewOffset.z < 10.0f) viewOffset.z = 64.0f;
        
        Vector3 eyePos = localOrigin + viewOffset;
        int localTeam = *(uint8_t*)(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);

        view_matrix_t viewMatrix;

        ImGuiIO& io = ImGui::GetIO();
        int screenWidth = (int)io.DisplaySize.x;
        int screenHeight = (int)io.DisplaySize.y;
        Vector2 screenCenter(screenWidth / 2.0f, screenHeight / 2.0f);

        float bestDist = g_fAimbotFov * 10.0f;
        QAngle bestTargetAngle;
        bool foundTarget = false;
        
        uintptr_t firstValidPawn = 0;
        Vector3 firstValidTargetPos;
        Vector2 firstValidScreenPos;
        float firstValidDist = 0;
        
        int validCount = 0;
        int boneFoundCount = 0;

        auto IsPawnVisible = [&](uintptr_t targetPawn, int observerIndex) -> bool {
            if (!g_bAimbotVisCheck) return true;
            uint64_t mask = *(uint64_t*)(targetPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_entitySpottedState +
                cs2_dumper::schemas::client_dll::EntitySpottedState_t::m_bSpottedByMask);
            return (mask & (1ULL << (observerIndex - 1))) != 0;
        };

        if (g_lockedPawn) {
            int hp = *(int*)(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
            int team = *(uint8_t*)(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
            bool bSpotted = IsPawnVisible(g_lockedPawn, localIndex);
            float flashDuration = *(float*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration);
            if (hp > 0 && (localTeam != team || !g_bAimbotTeamCheck) && (bSpotted || !g_bAimbotVisCheck) && (flashDuration <= 0.5f || !g_bAimbotFlashCheck)) {
                Vector3 targetPos = GetBonePosition(g_lockedPawn, g_nAimbotBone);
                if (targetPos.x != 0.0f || targetPos.y != 0.0f) {
                    Vector2 screenPos;
                    SPOOF_CALL(memcpy)(&viewMatrix, (void*)(clientBase + cs2_dumper::offsets::client_dll::dwViewMatrix), sizeof(view_matrix_t));
                    if (WorldToScreen(targetPos, screenPos, viewMatrix, screenWidth, screenHeight)) {
                        float dist = sqrtf(powf(screenPos.x - screenCenter.x, 2) + powf(screenPos.y - screenCenter.y, 2));
                        if (dist < bestDist * 1.8f) {
                            bestDist = dist;
                            bestTargetAngle = Vector3::CalculateAngle(eyePos, targetPos);
                            foundTarget = true;
                        } else {
                            g_lockedPawn = 0;
                        }
                    } else {
                        g_lockedPawn = 0;
                    }
                } else {
                    g_lockedPawn = 0;
                }
            } else {
                g_lockedPawn = 0;
            }
        }

        if (!foundTarget) {
            for (int i = 0; i < 64; i++) {
                uintptr_t listEntry = *(uintptr_t*)(entityList + 0x10 + 8 * (i >> 9));
                if (!listEntry) continue;

                uintptr_t controller = *(uintptr_t*)(listEntry + 0x70 * (i & 0x1FF));
                if (!controller) continue;

                if (controller == localController)
                {
                    localIndex = i;
                }

                uint32_t pawnHandle = *(uint32_t*)(controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn);
                if (!pawnHandle) continue;

                uintptr_t pawnEntry = *(uintptr_t*)(entityList + 0x10 + 8 * ((pawnHandle & 0x7FFF) >> 9));
                if (!pawnEntry) continue;

                uintptr_t pawn = *(uintptr_t*)(pawnEntry + 0x70 * (pawnHandle & 0x1FF));
                if (!pawn || pawn == localPawn) continue;

                int health = *(int*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
                uint8_t lifeState = *(uint8_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState);
                if (health <= 0 || lifeState != 0) continue;
                
                validCount++;

                uintptr_t gameSceneNode = *(uintptr_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode);
                uintptr_t boneArray = 0;
                if (gameSceneNode) {
                    boneArray = *(uintptr_t*)(gameSceneNode + 0x1E0);
                    if (!boneArray) boneArray = *(uintptr_t*)(gameSceneNode + 0x1F0);
                }

                if (boneArray) boneFoundCount++;

                if (g_bAimbotTeamCheck) {
                    int enemyTeamNum = *(uint8_t*)(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
                    if (localTeam == enemyTeamNum) continue;
                }

                if (g_bAimbotVisCheck) {
                    bool bSpotted = IsPawnVisible(pawn, localIndex);
                    if (!bSpotted) continue;
                }

                if (!boneArray) continue;

                if (g_bAimbotFlashCheck) {
                    float flashDuration = *(float*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration);
                    if (flashDuration > 0.5f) continue;
				}

                Vector3 targetPos = GetBonePosition(pawn, g_nAimbotBone);
                if (targetPos.x == 0.0f && targetPos.y == 0.0f) continue;

                Vector2 targetScreen;
                SPOOF_CALL(memcpy)(&viewMatrix, (void*)(clientBase + cs2_dumper::offsets::client_dll::dwViewMatrix), sizeof(view_matrix_t));
                bool onScreen = WorldToScreen(targetPos, targetScreen, viewMatrix, screenWidth, screenHeight);
                float dist = sqrtf(powf(targetScreen.x - screenCenter.x, 2) + powf(targetScreen.y - screenCenter.y, 2));

                if (firstValidPawn == 0) {
                    firstValidPawn = pawn;
                    firstValidTargetPos = targetPos;
                    firstValidScreenPos = targetScreen;
                    firstValidDist = dist;
                }

                if (dist < bestDist) {
                    bestDist = dist;
                    bestTargetAngle = Vector3::CalculateAngle(eyePos, targetPos);
                    foundTarget = true;
                    g_lockedPawn = pawn;
                }
            }
        }

        if (foundTarget) {

            QAngle currentAngles = *(QAngle*)(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles);
            Vector3 aimPunch = *(Vector3*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_aimPunchAngle);
            int shotsFired = *(int*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iShotsFired);
            if (g_bAimbotRcs && shotsFired >= 1) {
                bestTargetAngle.x -= aimPunch.x * 2.f;
                bestTargetAngle.y -= aimPunch.y * 2.f;
            }

            bestTargetAngle.Clamp();

            if (g_fAimbotSmooth > 1.0f) {
                QAngle delta = bestTargetAngle - currentAngles;
                delta.Clamp();
                currentAngles.x += delta.x / g_fAimbotSmooth;
                currentAngles.y += delta.y / g_fAimbotSmooth;
            } else {
                currentAngles = bestTargetAngle;
            }

            currentAngles.Clamp();

            int hp = *(int*)(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
            uint8_t lifeState = *(uint8_t*)(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState);
            if (hp <= 0 || lifeState != 0) {
                g_lockedPawn = 0;
                return;
            }

            *(QAngle*)(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles) = currentAngles;
        }
    }

    void RunTriggerbot() {
        SPOOF_FUNC;
        if (!IsPressed(g_nTriggerbotKey)) {
            return;
        }

        uintptr_t localPawn = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPawn) return;
        int crosshairId = *(int*)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iIDEntIndex);
        if (crosshairId <= 0) return;
        uintptr_t entityList = *(uintptr_t*)(clientBase + cs2_dumper::offsets::client_dll::dwEntityList);
        if (!entityList) {
            return;
        }
        int entityIndex = crosshairId & 0x7FFF;
        uintptr_t listEntry = *(uintptr_t*)(entityList + 0x10 + 8 * (entityIndex >> 9));
        if (!listEntry) {
            return;
        }

        uintptr_t entity = *(uintptr_t*)(listEntry + 0x70 * (entityIndex & 0x1FF));
        if (!entity) {
            return;
        }
        int entityHealth = *(int*)(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
        if (entityHealth <= 0) {
            return;
        }
        if (g_bTriggerbotTeamCheck) {
            int localTeam = *(uint8_t*)(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
            int entityTeam = *(uint8_t*)(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
            if (entityTeam != localTeam) {
                uintptr_t attackAddr = clientBase + cs2_dumper::buttons::attack;
                std::this_thread::sleep_for(std::chrono::milliseconds(g_nTriggerbotDelay));
                *(int*)attackAddr = 65537;
                std::this_thread::sleep_for(std::chrono::milliseconds(g_nTriggerbotDuration));
                *(int*)attackAddr = 16777472;
            }
        }
        else {
            uintptr_t attackAddr = clientBase + cs2_dumper::buttons::attack;
            std::this_thread::sleep_for(std::chrono::milliseconds(g_nTriggerbotDelay));
            *(int*)attackAddr = 65537;
            std::this_thread::sleep_for(std::chrono::milliseconds(g_nTriggerbotDuration));
            *(int*)attackAddr = 16777472;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(g_nTriggerbotDelay));
    }

    DWORD WINAPI AimbotThread(LPVOID lpParam) {
        SPOOF_FUNC;
        while (g_bRunning) {
            if (g_bAimbotEnabled) {
                RunAimbot();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    }

    DWORD WINAPI TriggerbotThread(LPVOID lpParam) {
        SPOOF_FUNC;
        while (g_bRunning) {
            if (g_bTriggerbotEnabled) {
                RunTriggerbot();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        return 0;
    }

    DWORD WINAPI BlatantThread(LPVOID lpParam) {
        SPOOF_FUNC;
        while (g_bRunning) {
            if (g_bGlowEnabled || g_bNoFlashEnabled) {
                RunBlatant();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    }

    void EraseHeaders(HINSTANCE hModule)
    {
        SPOOF_FUNC;
        PIMAGE_DOS_HEADER pDoH;
        PIMAGE_NT_HEADERS pNtH;
        DWORD i, ersize, protect;
        if (!hModule) return;
        DWORD dwOldProtect;
        SPOOF_CALL(VirtualProtect)((LPVOID)hModule, 1024, PAGE_READWRITE, &dwOldProtect);
        pDoH = (PIMAGE_DOS_HEADER)(hModule);
        pDoH->e_magic = 0;
        pNtH = (PIMAGE_NT_HEADERS)((LONG)hModule + ((PIMAGE_DOS_HEADER)hModule)->e_lfanew);
        // pNtH->Signature = 0;
        SPOOF_CALL(VirtualProtect)((LPVOID)hModule, 1024, dwOldProtect, &dwOldProtect);
        ersize = sizeof(IMAGE_DOS_HEADER);
        if (SPOOF_CALL(VirtualProtect)(pDoH, ersize, PAGE_READWRITE, &protect))
        {
            for (i = 0; i < ersize; i++)
                *(BYTE*)((BYTE*)pDoH + i) = 0;
        }
        ersize = sizeof(IMAGE_NT_HEADERS);
        if (pNtH && SPOOF_CALL(VirtualProtect)(pNtH, ersize, PAGE_READWRITE, &protect))
        {
            for (i = 0; i < ersize; i++)
                *(BYTE*)((BYTE*)pNtH + i) = 0;
        }
        return;
    }

    uint32_t CRC32(const void* data, size_t length) {
        SPOOF_FUNC;
        static uint32_t table[256];
        static bool initialized = false;
        if (!initialized) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t ch = i;
                for (size_t j = 0; j < 8; j++)
                    ch = (ch >> 1) ^ (0xEDB88320 & (-(int32_t)(ch & 1)));
                table[i] = ch;
            }
            initialized = true;
        }

        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* p = (const uint8_t*)data;
        while (length--)
            crc = (crc >> 8) ^ table[(crc ^ *p++) & 0xFF];
        return crc ^ 0xFFFFFFFF;
    }

    uint32_t g_OriginalTextHash = 0;

    bool CheckTextSectionIntegrity() {
        SPOOF_FUNC;
        uintptr_t moduleBase = (uintptr_t)(GetModuleHandleA)(NULL);
        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)moduleBase;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(moduleBase + dosHeader->e_lfanew);
        PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
        for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
            if (strcmp((const char*)section[i].Name, ".text") == 0) {
                void* sectionAddr = (void*)(moduleBase + section[i].VirtualAddress);
                size_t sectionSize = section[i].Misc.VirtualSize;
                uint32_t currentHash = CRC32(sectionAddr, sectionSize);
                if (g_OriginalTextHash == 0) {
                    g_OriginalTextHash = currentHash;
                    return true;
                }
                return (currentHash == g_OriginalTextHash);
            }
        }
        return false;
    }

    DWORD WINAPI InjectThread(LPVOID lpParam) {
        SPOOF_FUNC;
        if (!CheckTextSectionIntegrity()) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        g_iLoadStep++;
        if (!CheckTextSectionIntegrity()) return 0;
        EraseHeaders(g_hModule);
        UnlinkModuleFromPEB(g_hModule);
        if (!CheckTextSectionIntegrity()) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        g_iLoadStep++;
        if (!CheckTextSectionIntegrity()) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        g_iLoadStep++;
        if (!CheckTextSectionIntegrity()) return 0;
        g_bRunning = true;
        g_hAimbotThread = StartBypassThread((LPTHREAD_START_ROUTINE)AimbotThread, nullptr);
        if (!CheckTextSectionIntegrity()) return 0;
        g_hTriggerbotThread = StartBypassThread((LPTHREAD_START_ROUTINE)TriggerbotThread, nullptr);
        if (!CheckTextSectionIntegrity()) return 0;
        g_hBlatantThread = StartBypassThread((LPTHREAD_START_ROUTINE)BlatantThread, nullptr);
        if (!CheckTextSectionIntegrity()) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        g_bGlobalLoaded = true;
		g_bEspEnabled = true;
        return 0;
    }
}


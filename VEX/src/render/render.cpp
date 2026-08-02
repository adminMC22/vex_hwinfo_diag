#include <include/render/render.hpp>
#include <include/driver/driver_context.hpp>
#include <include/utils/logger.hpp>

// Win32 helper for ImGui backend
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace sky::render {

    RENDER::~RENDER() {
		shutdown();
	}

	void RENDER::create_target_view() {
		ID3D11Texture2D* back_buffer {};
		if (swap_chain && SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
			device->CreateRenderTargetView(back_buffer, nullptr, &render_target_view);
			back_buffer->Release();
		}
	}

	void RENDER::cleanup_target_view() {
        if (device_context) {
            device_context->OMSetRenderTargets(0, nullptr, nullptr);
        }
        if (render_target_view) { render_target_view->Release(); render_target_view = nullptr; }
	}

	bool RENDER::init() {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        Width = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
        Height = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));

        DEVMODE dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            Width = static_cast<int>(dm.dmPelsWidth);
            Height = static_cast<int>(dm.dmPelsHeight);
        }

        // Acquire a target window. The ESP renders at screen coordinates, so
        // the overlay works both attached to the game window and as a
        // standalone fullscreen window. Never fatal: retry for ~20s (the
        // game may start after Sky.exe) and fall back to our own window.
        static const char* overlay_classes[] = {
            "Windows.UI.Core.CoreWindow",            // spoofed system-ish name
            "Windows.UI.Composition.ContentBridge",  // spoofed system-ish name
            "SkyOverlay",                            // guaranteed private class
        };

        for (int attempt = 0; attempt < 10 && !hWnd; attempt++) {
            // 1) Valorant window (UnrealWindow class + UE title with padding)
            hWnd = FindWindowA(xorstr_("UnrealWindow"), xorstr_("VALORANT  "));
            if (hWnd) {
                LOG_INFO(std::format("overlay: game window 0x{:x}", (uintptr_t)hWnd));
                break;
            }

            // 2) Own transparent overlay. Note: "Windows.UI.Core.CoreWindow"
            //    is a REAL system class on Win10/11 — RegisterClassExA fails
            //    for it (ignored) and CreateWindowExA then fails too, which
            //    is why the old single-name fallback died with
            //    "Failed to create overlay window" when the game was down.
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = DefWindowProcA;
            wc.hInstance = GetModuleHandleA(nullptr);
            for (auto cls : overlay_classes) {
                wc.lpszClassName = cls;
                if (!RegisterClassExA(&wc) && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
                    continue;  // real system class (or duplicate) — try next name
                }
                hWnd = CreateWindowExA(
                    WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
                    cls, cls,
                    WS_POPUP,
                    0, 0, (int)Width, (int)Height,
                    nullptr, nullptr, wc.hInstance, nullptr
                );
                if (hWnd) {
                    LOG_INFO(std::format("overlay: own window 0x{:x}", (uintptr_t)hWnd));
                    break;
                }
            }

            if (!hWnd) {
                Sleep(2000);  // retry — the game may not be up yet
            }
        }

        if (!hWnd) {
            LOG_ERROR("Failed to create overlay window");
            MessageBoxA(0, xorstr_("Failed to create overlay window.\n\n"
                "Neither the Valorant window nor the fallback overlay could be created.\n"
                "Start Valorant first, then run Sky.exe."),
                xorstr_("Error"), MB_OK | MB_ICONERROR);
            return false;  // do not ExitProcess — the caller handles cleanup
        }

        SetMenu(hWnd, 0);

        SetWindowLongA(hWnd, -20, static_cast<LONG_PTR>((static_cast<int>(GetWindowLongA(hWnd, -20)) | 32)));
        MARGINS margin{ 0, 0, (GetSystemMetrics)(SM_CXSCREEN),(GetSystemMetrics)(SM_CYSCREEN) };
        DwmExtendFrameIntoClientArea(hWnd, &margin);
        SetLayeredWindowAttributes(hWnd, 0x00, 254, LWA_ALPHA);

        MoveWindow(hWnd, 0, 0, (GetSystemMetrics)(SM_CXSCREEN), (GetSystemMetrics)(SM_CYSCREEN), FALSE);

        UpdateWindow(hWnd);
        ShowWindow(hWnd, SW_SHOW);

        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 144;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const UINT createDeviceFlags = 0;
        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
        auto res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &swap_chain, &device, &featureLevel, &device_context);

		var->device_dx11 = device;
		var->swap_chain = swap_chain;
		var->device_context = device_context;

        cleanup_target_view();
        swap_chain->ResizeBuffers(0, Width, Height, DXGI_FORMAT_UNKNOWN, 0);
        create_target_view();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        ImFontConfig cfg;
        cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_Bitmap;
        cfg.FontDataOwnedByAtlas = false;

        io.Fonts->Clear();

        var->font.inter[0] = io.Fonts->AddFontFromMemoryTTF(inter_semibold_hex, sizeof inter_semibold_hex, SCALE(12.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.inter[1] = io.Fonts->AddFontFromMemoryTTF(inter_medium_hex, sizeof inter_medium_hex, SCALE(12.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.inter[2] = io.Fonts->AddFontFromMemoryTTF(inter_medium_hex, sizeof inter_medium_hex, SCALE(10.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.inter[3] = io.Fonts->AddFontFromMemoryTTF(inter_semibold_hex, sizeof inter_semibold_hex, SCALE(13.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());

        var->font.icons[0] = io.Fonts->AddFontFromMemoryTTF(logotype_hex, sizeof logotype_hex, SCALE(30.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[1] = io.Fonts->AddFontFromMemoryTTF(section_icons_hex, sizeof section_icons_hex, SCALE(18.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[2] = io.Fonts->AddFontFromMemoryTTF(icons_hex, sizeof icons_hex, SCALE(10.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[3] = io.Fonts->AddFontFromMemoryTTF(child_icons_hex, sizeof child_icons_hex, SCALE(12.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[4] = io.Fonts->AddFontFromMemoryTTF(icons_hex, sizeof icons_hex, SCALE(8.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[5] = io.Fonts->AddFontFromMemoryTTF(icons_hex, sizeof icons_hex, SCALE(11.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[6] = io.Fonts->AddFontFromMemoryTTF(keybind_icons_hex, sizeof keybind_icons_hex, SCALE(10.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[7] = io.Fonts->AddFontFromMemoryTTF(keybind_icons_hex, sizeof keybind_icons_hex, SCALE(5.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[8] = io.Fonts->AddFontFromMemoryTTF(icons_hex, sizeof icons_hex, SCALE(15.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());
        var->font.icons[9] = io.Fonts->AddFontFromMemoryTTF(color_icons_hex, sizeof color_icons_hex, SCALE(11.f), &cfg, io.Fonts->GetGlyphRangesCyrillic());

        io.Fonts->Build();

        ImGui_ImplWin32_Init(hWnd);
        ImGui_ImplDX11_Init(device, device_context);
		return true;
	}

	void RENDER::begin() {

		Width = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
		Height = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));

        static bool streamer_mode_set = var->config.esp.streamer_mode;
        if (streamer_mode_set != var->config.esp.streamer_mode) {
            streamer_mode_set = var->config.esp.streamer_mode;
            SetWindowDisplayAffinity(hWnd, streamer_mode_set ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
        }

		MSG msg;
		while (::PeekMessageA(&msg, 0, 0U, 0U, 0x0001)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

        gui->render();
	}

    void RENDER::end() {
        ImGui::Render();
        UpdateKeyStates();
        UpdateMouseState();

        const float clear_color_with_alpha[4] = { 0.f, 0.f, 0.f, 0.f };
        device_context->OMSetRenderTargets(1, &render_target_view, NULL);
        device_context->ClearRenderTargetView(render_target_view, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swap_chain->Present(0, 0);
	}

	void RENDER::shutdown() {
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		cleanup_target_view();
		if (swap_chain) { swap_chain->Release(); swap_chain = nullptr; }
		if (device_context) { device_context->Release(); device_context = nullptr; }
		if (device) { device->Release(); device = nullptr; }
	}

	float RENDER::get_fps() const {
		return ImGui::GetIO().Framerate;
	}

	void RENDER::UpdateKeyStates() {
		ImGuiIO& io = ImGui::GetIO();
		for (int i = 0; i < 0x100; ++i) {
            auto isPushed = IsKeyPushed(i);
			io.AddKeyEvent(ImGuiKey(i), isPushed);
            if (i == VK_INSERT && isPushed)
                var->gui.enabled = !var->gui.enabled;
            if(i == VK_END && isPushed)
				ExitProcess(0);
		}
	}

	void RENDER::UpdateMouseState() {
		ImGuiIO& io = ImGui::GetIO();

		POINT cursor_pos{};
		GetCursorPos(&cursor_pos);
		io.MousePos = ImVec2(static_cast<float>(cursor_pos.x), static_cast<float>(cursor_pos.y));

		io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(3, (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0);
		io.AddMouseButtonEvent(4, (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0);
	}

} // namespace sky::ui
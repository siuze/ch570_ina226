#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <winrt/Windows.Foundation.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "implot.h"

#include "ui/style.hpp"
#include "ui/dashboard.hpp"
#include "ble_scanner.hpp"
#include "serial_manager.hpp"
#include "resource.h"
#include "app_icon_data.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Direct3D 11 state
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

namespace CH570App {
ID3D11ShaderResourceView* g_pIconTextureView = nullptr;
}

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void CreateIconTexture(ID3D11Device* device);
void SaveBackBufferToBMP(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain, const char* filename);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // Enable High-DPI awareness so Windows does not blur text by bitmap stretching
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pfn = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn) {
            pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    }

    // Initialize C++/WinRT Apartment for BLE
    winrt::init_apartment();

    // Query Desktop Work Area and DPI to adjust window size according to user's desired proportion
    float dpiScale = 1.0f;
    HDC screenDc = GetDC(NULL);
    if (screenDc) {
        dpiScale = (float)GetDeviceCaps(screenDc, LOGPIXELSX) / 96.0f;
        ReleaseDC(NULL, screenDc);
    }
    if (dpiScale < 1.0f) dpiScale = 1.0f;

    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenW = workArea.right - workArea.left;
    int screenH = workArea.bottom - workArea.top;

    // Matching user's screenshot proportion (~72% width, ~88% height, with comfortable bounds)
    int winW = (int)(screenW * 0.72f);
    int winH = (int)(screenH * 0.88f);
    if (winW < 1080) winW = 1080;
    if (winH < 760) winH = 760;
    if (winW > screenW - 20) winW = screenW - 20;
    if (winH > screenH - 40) winH = screenH - 40;

    int posX = workArea.left + (screenW - winW) / 2;
    int posY = workArea.top + (screenH - winH) / 2;

    // Load custom application icon
    HICON hAppIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    HICON hAppIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_SHARED);

    // Register Win32 Window Class
    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L, 0L,
        hInstance,
        hAppIcon, nullptr, nullptr, nullptr,
        L"CH570MonitorWindowClass",
        hAppIconSm
    };
    ::RegisterClassExW(&wc);

    // Create Application Window
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName,
        L"CH570 无线串口 & 实时电流功耗监测器 v2.0.8",
        WS_OVERLAPPEDWINDOW,
        posX, posY, winW, winH,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Initialize Direct3D 11
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Create in-app icon texture
    CreateIconTexture(g_pd3dDevice);

    ::ShowWindow(hwnd, (nCmdShow == 0 || nCmdShow == SW_HIDE) ? SW_SHOW : nCmdShow);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Apply Modern Daylight Clean UI Style & Crisp High-Res Fonts
    CH570App::UIStyle::ApplyModernLightTheme(dpiScale);
    CH570App::UIStyle::LoadFonts(14.0f, dpiScale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    CH570App::BleScanner bleScanner;
    bleScanner.Start();

    CH570App::SerialManager::Instance().AutoDetectDongle();

    CH570App::Dashboard dashboard(bleScanner);

    // WinUI 3 / Fluent 2 Canvas Clear Color (#F3F3F3)
    const float clear_color[4] = { 0.953f, 0.953f, 0.953f, 1.000f };

    const char* cmdLine = GetCommandLineA();
    bool isScreenshotMode = (cmdLine && strstr(cmdLine, "screenshot") != nullptr) || (getenv("CH570_SCREENSHOT") != nullptr);
    int screenshotFrameCount = 0;

    // Main loop
    bool bDone = false;
    while (!bDone) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                bDone = true;
            }
        }
        if (bDone) break;

        // Handle window resizing
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render Dashboard
        dashboard.Render();

        // Rendering
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (isScreenshotMode) {
            screenshotFrameCount++;
            if (screenshotFrameCount >= 5) {
                SaveBackBufferToBMP(g_pd3dDevice, g_pd3dDeviceContext, g_pSwapChain, "winui3_rendered.bmp");
                bDone = true;
            }
        }

        // Present with VSync enabled (limits CPU to negligible <0.5%)
        g_pSwapChain->Present(1, 0);
    }

    // Cleanup subsystems
    bleScanner.Stop();
    CH570App::SerialManager::Instance().CloseDongleCOM2();
    CH570App::SerialManager::Instance().CloseCOM1();

    // ImGui cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    if (CH570App::g_pIconTextureView) {
        CH570App::g_pIconTextureView->Release();
        CH570App::g_pIconTextureView = nullptr;
    }

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    winrt::uninit_apartment();
    return 0;
}

// -------------------------------------------------------------
// Icon Texture Helper
// -------------------------------------------------------------
void CreateIconTexture(ID3D11Device* device) {
    if (!device) return;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = CH570App::g_IconW;
    desc.Height = CH570App::g_IconH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = CH570App::g_IconPixels;
    subResource.SysMemPitch = CH570App::g_IconW * 4;
    subResource.SysMemSlicePitch = 0;

    ID3D11Texture2D* pTexture = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &subResource, &pTexture))) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        device->CreateShaderResourceView(pTexture, &srvDesc, &CH570App::g_pIconTextureView);
        pTexture->Release();
    }
}

// -------------------------------------------------------------
// Direct3D 11 Helper Functions
// -------------------------------------------------------------
void SaveBackBufferToBMP(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain, const char* filename) {
    FILE* flog = fopen("debug_screenshot.log", "a");
    if (flog) { fprintf(flog, "SaveBackBufferToBMP entry. filename=%s\n", filename ? filename : "null"); fclose(flog); }

    if (!device || !context || !swapChain || !filename) return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) {
        FILE* f = fopen("debug_screenshot.log", "a");
        if (f) { fprintf(f, "GetBuffer failed: 0x%08X\n", (unsigned int)hr); fclose(f); }
        return;
    }

    D3D11_TEXTURE2D_DESC desc;
    pBackBuffer->GetDesc(&desc);
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;

    ID3D11Texture2D* pStaging = nullptr;
    hr = device->CreateTexture2D(&desc, nullptr, &pStaging);
    if (FAILED(hr)) {
        FILE* f = fopen("debug_screenshot.log", "a");
        if (f) { fprintf(f, "CreateTexture2D failed: 0x%08X\n", (unsigned int)hr); fclose(f); }
        pBackBuffer->Release();
        return;
    }

    context->CopyResource(pStaging, pBackBuffer);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        FILE* fp = fopen(filename, "wb");
        if (fp) {
            uint32_t width = desc.Width;
            uint32_t height = desc.Height;
            uint32_t rowSize = ((width * 3 + 3) / 4) * 4;
            uint32_t dataSize = rowSize * height;

            BITMAPFILEHEADER bfh = {};
            bfh.bfType = 0x4D42; // "BM"
            bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            bfh.bfSize = bfh.bfOffBits + dataSize;

            BITMAPINFOHEADER bih = {};
            bih.biSize = sizeof(BITMAPINFOHEADER);
            bih.biWidth = width;
            bih.biHeight = height; // Bottom-up
            bih.biPlanes = 1;
            bih.biBitCount = 24;
            bih.biCompression = BI_RGB;
            bih.biSizeImage = dataSize;

            fwrite(&bfh, sizeof(bfh), 1, fp);
            fwrite(&bih, sizeof(bih), 1, fp);

            std::vector<uint8_t> rowBuf(rowSize, 0);
            const uint8_t* srcPixels = (const uint8_t*)mapped.pData;

            for (int y = (int)height - 1; y >= 0; --y) {
                const uint8_t* rowSrc = srcPixels + y * mapped.RowPitch;
                for (uint32_t x = 0; x < width; ++x) {
                    rowBuf[x * 3 + 0] = rowSrc[x * 4 + 2]; // B (from RGBA B)
                    rowBuf[x * 3 + 1] = rowSrc[x * 4 + 1]; // G (from RGBA G)
                    rowBuf[x * 3 + 2] = rowSrc[x * 4 + 0]; // R (from RGBA R)
                }
                fwrite(rowBuf.data(), 1, rowSize, fp);
            }
            fclose(fp);

            FILE* f = fopen("debug_screenshot.log", "a");
            if (f) { fprintf(f, "Successfully written %s (%ux%u)\n", filename, width, height); fclose(f); }
        } else {
            FILE* f = fopen("debug_screenshot.log", "a");
            if (f) { fprintf(f, "Failed to open output file: %s\n", filename); fclose(f); }
        }
        context->Unmap(pStaging, 0);
    } else {
        FILE* f = fopen("debug_screenshot.log", "a");
        if (f) { fprintf(f, "Map failed: 0x%08X\n", (unsigned int)hr); fclose(f); }
    }

    pStaging->Release();
    pBackBuffer->Release();
}
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
        );
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0; // Disable ALT application menu
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

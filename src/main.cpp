#include "app.h"
#include "capture.h"
#include "glass.h"
#include "overlay.h"
#include "resource.h"
#include "update.h"
#include "version.h"

#include <shlobj.h>

#include <cstring>
#include <cwchar>

AppState g_app;

void RefreshMainBackdropForDrag(HWND hwnd);
bool CaptureMainBackdropNow();

namespace {

constexpr float kClientWidth = 400.0f;
constexpr float kHomeClientHeight = 240.0f;
constexpr float kSettingsClientHeight = 560.0f;
constexpr float kTitleBarHeight = 44.0f;

constexpr UINT kTrayCallbackMessage = WM_APP + 20;
constexpr UINT kShowMainMessage = WM_APP + 21;
constexpr UINT kQuitForInstallerMessage = WM_APP + 22;
constexpr UINT kUpdateCheckDoneMessage = WM_APP + 30;
constexpr UINT kUpdateReadyMessage = WM_APP + 31;
constexpr UINT kHotkeyFullscreenId = 1;
constexpr UINT kHotkeyRegionId = 2;
constexpr UINT_PTR kViewAnimationTimer = 2;
constexpr UINT_PTR kUpdateLaunchTimer = 3;
constexpr UINT kViewAnimationDurationMs = 200;
constexpr UINT kUpdateLaunchTimeoutMs = 90000;
constexpr DWORD kDwmaCloak = 13;  // DWMWA_CLOAK
constexpr int kMainGlassFactor = 20;

enum TrayCommand : UINT {
    kTrayOpen = 1,
    kTrayFullscreen,
    kTrayRegion,
    kTraySettings,
    kTrayToggleCloseToTray,
    kTrayToggleAutoStart,
    kTrayExit,
};

enum ControlId : int {
    kControlNone = -1,
    kFullScreen = 0,
    kRegion = 1,
    kSettings = 2,
    kMinimize = 3,
    kClose = 4,
    kBack = 10,
    kThemeSystem = 11,
    kThemeLight = 12,
    kThemeDark = 13,
    kCloseToTrayControl = 14,
    kAutoStartControl = 15,
    kHotkeyFullscreenControl = 16,
    kHotkeyRegionControl = 17,
    kUpdateButton = 18,
    kGlassNone = 19,
    kGlassStatic = 20,
    kGlassDynamic = 21,
};

const D2D1_RECT_F kTileRects[2] = {
    D2D1::RectF(24.0f, 56.0f, 192.0f, 212.0f),
    D2D1::RectF(208.0f, 56.0f, 376.0f, 212.0f),
};
const D2D1_RECT_F kTitleSettingsRect = D2D1::RectF(268.0f, 0.0f, 312.0f, 44.0f);
const D2D1_RECT_F kMinimizeRect = D2D1::RectF(312.0f, 0.0f, 356.0f, 44.0f);
const D2D1_RECT_F kCloseRect = D2D1::RectF(356.0f, 0.0f, 400.0f, 44.0f);
const D2D1_RECT_F kAppMarkRect = D2D1::RectF(16.0f, 11.0f, 38.0f, 33.0f);
const D2D1_RECT_F kBackRect = D2D1::RectF(44.0f, 4.0f, 80.0f, 40.0f);
const D2D1_RECT_F kTitleTextRect = D2D1::RectF(84.0f, 0.0f, 264.0f, 44.0f);
const D2D1_RECT_F kThemeRects[3] = {
    D2D1::RectF(24.0f, 82.0f, 136.0f, 116.0f),
    D2D1::RectF(144.0f, 82.0f, 256.0f, 116.0f),
    D2D1::RectF(264.0f, 82.0f, 376.0f, 116.0f),
};
const D2D1_RECT_F kGlassRects[3] = {
    D2D1::RectF(24.0f, 124.0f, 136.0f, 158.0f),
    D2D1::RectF(144.0f, 124.0f, 256.0f, 158.0f),
    D2D1::RectF(264.0f, 124.0f, 376.0f, 158.0f),
};
const D2D1_RECT_F kCloseToTrayRow = D2D1::RectF(24.0f, 206.0f, 376.0f, 240.0f);
const D2D1_RECT_F kCloseToTraySwitch = D2D1::RectF(328.0f, 210.0f, 376.0f, 236.0f);
const D2D1_RECT_F kAutoStartRow = D2D1::RectF(24.0f, 244.0f, 376.0f, 278.0f);
const D2D1_RECT_F kAutoStartSwitch = D2D1::RectF(328.0f, 248.0f, 376.0f, 274.0f);
const D2D1_RECT_F kHotkeyFullscreenButton = D2D1::RectF(196.0f, 322.0f, 376.0f, 356.0f);
const D2D1_RECT_F kHotkeyRegionButton = D2D1::RectF(196.0f, 360.0f, 376.0f, 394.0f);
const D2D1_RECT_F kUpdateButtonRect = D2D1::RectF(236.0f, 454.0f, 376.0f, 486.0f);

float ClientHeightForView() {
    return g_app.view == MainView::Settings ? kSettingsClientHeight : kHomeClientHeight;
}

bool IsDarkThemePreferred() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status == ERROR_SUCCESS) return value == 0;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD data = 0;
        DWORD dataSize = sizeof(data);
        DWORD type = 0;
        const LSTATUS queried = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                                 reinterpret_cast<LPBYTE>(&data), &dataSize);
        RegCloseKey(key);
        if (queried == ERROR_SUCCESS && type == REG_DWORD) return data == 0;
    }
    return false;
}


void SetDwmAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size) {
    if (hwnd) DwmSetWindowAttribute(hwnd, attribute, value, size);
}

struct OsVersionInfo {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform;
    wchar_t servicePack[128];
};

bool IsWindows11OrGreater() {
    using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    FARPROC procedure = ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    RtlGetVersionFn getVersion = nullptr;
    std::memcpy(&getVersion, &procedure, sizeof(getVersion));
    if (!getVersion) return false;
    OsVersionInfo info{};
    info.size = sizeof(info);
    if (getVersion(&info) != 0) return false;
    return info.major >= 10 && info.build >= 22000;
}

bool ShouldUseComposition() {
    static const bool supported = IsWindows11OrGreater();
    return supported;
}

void UpdatePalette();
bool CreateGlassBitmap();

bool CreateRenderBrush() {
    g_app.brush.Reset();
    if (!g_app.rt) return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(g_app.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush.Put())) ||
        !brush) {
        return false;
    }
    g_app.brush = std::move(brush);
    return true;
}

bool CreateCompositionTargetBitmap() {
    if (!g_app.d2dContext || !g_app.swapChain) return false;
    g_app.d2dContext->SetTarget(nullptr);
    g_app.targetBitmap.Reset();

    ComPtr<IDXGISurface> surface;
    if (FAILED(g_app.swapChain->GetBuffer(
            0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.Put()))) ||
        !surface) {
        return false;
    }

    D2D1_BITMAP_PROPERTIES1 properties{};
    properties.pixelFormat =
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    properties.dpiX = static_cast<float>(g_app.dpi);
    properties.dpiY = static_cast<float>(g_app.dpi);
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    if (FAILED(g_app.d2dContext->CreateBitmapFromDxgiSurface(
            surface.Get(), &properties, g_app.targetBitmap.Put())) ||
        !g_app.targetBitmap) {
        return false;
    }
    g_app.d2dContext->SetTarget(g_app.targetBitmap.Get());
    return true;
}

enum class WindowCompositionAttribute : int { AccentPolicy = 19 };

struct AccentPolicy {
    int accentState;
    int accentFlags;
    DWORD gradientColor;
    int animationId;
};

struct WindowCompositionAttributeData {
    WindowCompositionAttribute attribute;
    void* data;
    size_t size;
};

using SetWindowCompositionAttributeFn =
    BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);

SetWindowCompositionAttributeFn AccentAttributeSetter() {
    static const SetWindowCompositionAttributeFn setAttribute = [] {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        FARPROC procedure = user32 ? GetProcAddress(user32, "SetWindowCompositionAttribute")
                                   : nullptr;
        SetWindowCompositionAttributeFn function = nullptr;
        if (procedure) std::memcpy(&function, &procedure, sizeof(function));
        return function;
    }();
    return setAttribute;
}

bool SetAccentPolicy(HWND hwnd, int state, DWORD gradientColor) {
    SetWindowCompositionAttributeFn setAttribute = AccentAttributeSetter();
    if (!setAttribute) return false;
    AccentPolicy policy{};
    policy.accentState = state;
    policy.accentFlags = state == 0 ? 0 : 2;
    policy.gradientColor = gradientColor;
    WindowCompositionAttributeData data{WindowCompositionAttribute::AccentPolicy, &policy,
                                        sizeof(policy)};
    return setAttribute(hwnd, &data) != FALSE;
}

bool ApplyAccentAcrylic(HWND hwnd) {
    return SetAccentPolicy(hwnd, 4,  // ACCENT_ENABLE_ACRYLICBLURBEHIND
                           g_app.dark ? 0x66000000u : 0x66F3F3F3u);
}

void DisableSystemBackdrop(HWND hwnd) {
    if (!hwnd) return;
    SetAccentPolicy(hwnd, 0, 0);
    int backdrop = 1;  // DWMSBT_NONE
    DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
    MARGINS margins{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    g_app.backdropEnabled = false;
    g_app.dynamicBackdrop = false;
}

bool EnableDynamicBackdrop(HWND hwnd) {
    if (!hwnd || !g_app.compositionMode) return false;

    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    int none = 1;  // DWMSBT_NONE
    DwmSetWindowAttribute(hwnd, 38, &none, sizeof(none));
    SetAccentPolicy(hwnd, 0, 0);

    // The documented Windows 11 Acrylic backdrop samples the desktop live in the
    // compositor, so it follows video and drags without any app-side capture.
    bool applied = false;
    int backdrop = 3;  // DWMSBT_TRANSIENTWINDOW (Acrylic)
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop)))) {
        applied = true;
    } else {
        applied = ApplyAccentAcrylic(hwnd);
    }

    g_app.backdropEnabled = applied;
    g_app.dynamicBackdrop = applied;
    UpdatePalette();
    InvalidateRect(hwnd, nullptr, FALSE);
    return applied;
}

void EnableStaticBackdropFallback(HWND hwnd) {
    if (!hwnd || !g_app.compositionMode) return;

    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    bool applied = ApplyAccentAcrylic(hwnd);
    if (!applied) {
        BOOL hostBackdrop = TRUE;
        DwmSetWindowAttribute(hwnd, 40, &hostBackdrop, sizeof(hostBackdrop));
        int backdrop = 3;  // DWMSBT_TRANSIENTWINDOW (system Acrylic)
        applied = SUCCEEDED(DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop)));
    }

    g_app.backdropEnabled = applied;
    g_app.dynamicBackdrop = false;
    UpdatePalette();
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool InitializeCompositionRenderer(HWND hwnd, UINT width, UINT height) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<ID3D11Device> device;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, device.Put(), nullptr,
                                   deviceContext.Put());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, device.Put(), nullptr,
                               deviceContext.Put());
    }
    if (FAILED(hr) || !device) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice),
                                      reinterpret_cast<void**>(dxgiDevice.Put()))) ||
        !dxgiDevice) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.Put())) || !adapter) return false;
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                                  reinterpret_cast<void**>(dxgiFactory.Put()))) ||
        !dxgiFactory) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width;
    description.Height = height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    ComPtr<IDXGISwapChain1> swapChain;
    if (FAILED(dxgiFactory->CreateSwapChainForComposition(device.Get(), &description, nullptr,
                                                          swapChain.Put())) ||
        !swapChain) {
        return false;
    }

    ComPtr<ID2D1Factory1> d2dFactory;
    if (FAILED(g_app.d2d->QueryInterface(__uuidof(ID2D1Factory1),
                                         reinterpret_cast<void**>(d2dFactory.Put()))) ||
        !d2dFactory) {
        return false;
    }
    ComPtr<ID2D1Device> d2dDevice;
    if (FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.Put())) || !d2dDevice) {
        return false;
    }
    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.Put())) ||
        !context) {
        return false;
    }

    ComPtr<IDCompositionDevice> compositionDevice;
    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                                        reinterpret_cast<void**>(compositionDevice.Put()))) ||
        !compositionDevice) {
        return false;
    }
    ComPtr<IDCompositionTarget> compositionTarget;
    if (FAILED(compositionDevice->CreateTargetForHwnd(hwnd, TRUE, compositionTarget.Put())) ||
        !compositionTarget) {
        return false;
    }
    ComPtr<IDCompositionVisual> compositionVisual;
    if (FAILED(compositionDevice->CreateVisual(compositionVisual.Put())) || !compositionVisual) {
        return false;
    }
    if (FAILED(compositionVisual->SetContent(swapChain.Get())) ||
        FAILED(compositionTarget->SetRoot(compositionVisual.Get())) ||
        FAILED(compositionDevice->Commit())) {
        return false;
    }

    g_app.d3dDevice = std::move(device);
    g_app.swapChain = std::move(swapChain);
    g_app.compositionDevice = std::move(compositionDevice);
    g_app.compositionTarget = std::move(compositionTarget);
    g_app.compositionVisual = std::move(compositionVisual);
    g_app.d2dContext = context.Get();
    g_app.rt.Attach(context.Detach());
    g_app.rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    g_app.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    g_app.rt->SetDpi(static_cast<float>(g_app.dpi), static_cast<float>(g_app.dpi));
    if (!CreateCompositionTargetBitmap() || !CreateRenderBrush()) {
        g_app.d2dContext = nullptr;
        g_app.rt.Reset();
        g_app.swapChain.Reset();
        g_app.compositionDevice.Reset();
        g_app.compositionTarget.Reset();
        g_app.compositionVisual.Reset();
        g_app.targetBitmap.Reset();
        g_app.d3dDevice.Reset();
        return false;
    }

    g_app.compositionMode = true;
    CreateGlassBitmap();
    if (g_app.glassMode == GlassMode::Dynamic) {
        EnableDynamicBackdrop(hwnd);
    } else if (GlassEnabled() && !g_app.glassAvailable) {
        EnableStaticBackdropFallback(hwnd);
    }
    UpdatePalette();
    return true;
}

bool InitializeHwndRenderer(HWND hwnd, UINT width, UINT height) {
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);

    ComPtr<ID2D1HwndRenderTarget> target;
    const HRESULT hr = g_app.d2d->CreateHwndRenderTarget(
        properties, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)),
        target.Put());
    if (FAILED(hr) || !target) return false;

    target->SetDpi(static_cast<float>(g_app.dpi), static_cast<float>(g_app.dpi));
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    g_app.hwndTarget = target.Get();
    g_app.rt.Attach(target.Detach());
    g_app.compositionMode = false;
    g_app.backdropEnabled = false;
    if (!CreateRenderBrush()) {
        g_app.hwndTarget = nullptr;
        g_app.rt.Reset();
        return false;
    }
    CreateGlassBitmap();
    UpdatePalette();
    return true;
}

void EnsureRenderTarget(HWND hwnd) {
    if (g_app.rt || !g_app.d2d) return;

    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
    const UINT height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

    if (ShouldUseComposition()) {
        if (InitializeCompositionRenderer(hwnd, width, height)) return;
    }
    InitializeHwndRenderer(hwnd, width, height);
}

void ResizeRenderTarget(UINT width, UINT height) {
    if (g_app.compositionMode && g_app.d2dContext && g_app.swapChain) {
        g_app.d2dContext->SetTarget(nullptr);
        g_app.targetBitmap.Reset();
        if (SUCCEEDED(g_app.swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
            CreateCompositionTargetBitmap();
            if (g_app.compositionDevice) g_app.compositionDevice->Commit();
        }
        return;
    }
    if (g_app.hwndTarget) {
        g_app.hwndTarget->Resize(D2D1::SizeU(width, height));
    }
}

void DiscardRenderTarget() {
    g_app.brush.Reset();
    g_app.hwndTarget = nullptr;
    g_app.d2dContext = nullptr;
    g_app.rt.Reset();
    g_app.glassBitmap.Reset();
    g_app.glassAvailable = false;
    g_app.targetBitmap.Reset();
    g_app.compositionVisual.Reset();
    g_app.compositionTarget.Reset();
    g_app.compositionDevice.Reset();
    g_app.swapChain.Reset();
    g_app.d3dDevice.Reset();
    g_app.compositionMode = false;
    g_app.backdropEnabled = false;
}

void UpdatePalette() {
    g_app.palette = MakePalette(g_app.dark);
    if (!GlassEnabled()) return;

    const bool live =
        g_app.compositionMode && g_app.glassMode == GlassMode::Dynamic && g_app.dynamicBackdrop;
    if (!live && !g_app.backdropEnabled && !g_app.glassAvailable) return;
    if (live) {
        // The compositor already supplies the Acrylic tint and blur; keep our own
        // layers light so the live background stays visible.
        g_app.palette.page = WithAlpha(g_app.palette.page, g_app.dark ? 0.18f : 0.30f);
        g_app.palette.card = WithAlpha(g_app.palette.card, g_app.dark ? 0.56f : 0.70f);
        g_app.palette.cardHover = WithAlpha(g_app.palette.cardHover, g_app.dark ? 0.66f : 0.78f);
        g_app.palette.cardPressed = WithAlpha(g_app.palette.cardPressed, g_app.dark ? 0.62f : 0.74f);
        g_app.palette.border = WithAlpha(g_app.palette.border, 0.58f);
        g_app.palette.footerLine = WithAlpha(g_app.palette.footerLine, 0.54f);
    } else {
        g_app.palette.page = WithAlpha(g_app.palette.page, g_app.dark ? 0.42f : 0.62f);
        g_app.palette.card = WithAlpha(g_app.palette.card, g_app.dark ? 0.72f : 0.80f);
        g_app.palette.cardHover = WithAlpha(g_app.palette.cardHover, g_app.dark ? 0.82f : 0.88f);
        g_app.palette.cardPressed = WithAlpha(g_app.palette.cardPressed, g_app.dark ? 0.78f : 0.84f);
        g_app.palette.border = WithAlpha(g_app.palette.border, 0.68f);
        g_app.palette.footerLine = WithAlpha(g_app.palette.footerLine, 0.64f);
    }
    if (!g_app.dark) {
        g_app.palette.secondary = Rgb(0x3F3F3F);
        g_app.palette.tertiary = Rgb(0x666666);
    }
}

bool CreateGlassBitmap() {
    g_app.glassBitmap.Reset();
    if (!GlassEnabled() || !g_app.rt || g_app.glassWidth <= 0 || g_app.glassHeight <= 0 ||
        g_app.glassPixels.empty()) {
        g_app.glassAvailable = false;
        UpdatePalette();
        return false;
    }

    const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    const HRESULT hr = g_app.rt->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(g_app.glassWidth),
                    static_cast<UINT32>(g_app.glassHeight)),
        g_app.glassPixels.data(), static_cast<UINT32>(g_app.glassWidth) * 4u, properties,
        g_app.glassBitmap.Put());
    g_app.glassAvailable = SUCCEEDED(hr) && g_app.glassBitmap;
    UpdatePalette();
    return g_app.glassAvailable;
}

bool DrawGlassBackground(ID2D1RenderTarget* rt, HWND hwnd) {
    if (!rt || !g_app.glassBitmap || g_app.glassScreenWidth <= 0 ||
        g_app.glassScreenHeight <= 0) {
        return false;
    }
    RECT client{};
    GetClientRect(hwnd, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0) return false;

    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    const float xScale = static_cast<float>(g_app.glassWidth) / g_app.glassScreenWidth;
    const float yScale = static_cast<float>(g_app.glassHeight) / g_app.glassScreenHeight;
    const float sourceLeft = (origin.x - g_app.glassScreenX) * xScale;
    const float sourceTop = (origin.y - g_app.glassScreenY) * yScale;
    const float sourceRight = sourceLeft + clientWidth * xScale;
    const float sourceBottom = sourceTop + clientHeight * yScale;
    const D2D1_RECT_F source = D2D1::RectF(
        std::max(0.0f, sourceLeft), std::max(0.0f, sourceTop),
        std::min(static_cast<float>(g_app.glassWidth), sourceRight),
        std::min(static_cast<float>(g_app.glassHeight), sourceBottom));
    if (source.right <= source.left || source.bottom <= source.top) return false;

    const float scale = 96.0f / static_cast<float>(g_app.dpi);
    const D2D1_RECT_F destination =
        D2D1::RectF(0.0f, 0.0f, clientWidth * scale, clientHeight * scale);
    rt->DrawBitmap(g_app.glassBitmap.Get(), destination, 1.0f,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
    SetBrushColor(g_app.brush.Get(), g_app.palette.page);
    rt->FillRectangle(destination, g_app.brush.Get());
    return true;
}

bool IsFocused(int control) {
    return g_app.keyboardFocus && g_app.focusControl == control && GetFocus() == g_app.hwnd;
}

bool UsesHandCursor(int control) {
    return control >= 0 && control != kMinimize && control != kClose;
}

void CancelHotkeyCapture(bool restoreHotkeys);
void BeginHotkeyCapture(bool fullscreen);
void SetCloseToTrayEnabled(bool enabled);
void SetGlassMode(GlassMode mode);
void ToggleAutoStart();
void ShowMainFromTray(HWND hwnd);
void HideToTray(HWND hwnd);
void QuitApplication(HWND hwnd);
void UpdateTrayPresence(HWND hwnd);
void ShowTrayBalloon(const std::wstring& title, const std::wstring& text, bool error);
void RegisterCurrentHotkeys(bool showErrors);
void UnregisterConfiguredHotkeys();
void HandleHotkeyCaptureKey(WPARAM vk);
void ShowTrayMenu(HWND hwnd);
void ResizeWindowForView(HWND hwnd);
void BeginUpdateAction(HWND hwnd);

void ResizeWindowForView(HWND hwnd) {
    if (!hwnd) return;
    const UINT dpi = DpiOfWindow(hwnd);
    const int width = MulDiv(static_cast<int>(kClientWidth), dpi, 96);
    const int height = MulDiv(static_cast<int>(ClientHeightForView()), dpi, 96);

    RECT work{};
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
        work = info.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    RECT current{};
    GetWindowRect(hwnd, &current);
    int x = current.left;
    int y = current.top;
    if (x + width > work.right) x = work.right - width;
    if (x < work.left) x = work.left;
    if (y + height > work.bottom) y = work.bottom - height;
    if (y < work.top) y = work.top;
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void StopViewAnimation(HWND hwnd) {
    if (!g_app.viewAnimationActive) return;
    g_app.viewAnimationActive = false;
    if (hwnd) KillTimer(hwnd, kViewAnimationTimer);
}

void CompleteViewAnimation(HWND hwnd) {
    if (!g_app.viewAnimationActive) return;
    StopViewAnimation(hwnd);
    ResizeWindowForView(hwnd);
}

void StartViewAnimation(HWND hwnd) {
    if (!hwnd) return;
    StopViewAnimation(hwnd);

    const UINT dpi = DpiOfWindow(hwnd);
    const int width = MulDiv(static_cast<int>(kClientWidth), dpi, 96);
    const int targetHeight = MulDiv(static_cast<int>(ClientHeightForView()), dpi, 96);
    RECT current{};
    GetWindowRect(hwnd, &current);
    const int startHeight = current.bottom - current.top;

    if (startHeight == targetHeight) {
        ResizeWindowForView(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    RECT work{};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        work = monitor.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    int x = current.left;
    int y = current.top;
    if (x + width > work.right) x = work.right - width;
    if (x < work.left) x = work.left;
    if (y + targetHeight > work.bottom) y = work.bottom - targetHeight;
    if (y < work.top) y = work.top;

    EnsureRenderTarget(hwnd);
    if (g_app.compositionMode && g_app.rt) {
        ResizeRenderTarget(static_cast<UINT>(width),
                           static_cast<UINT>(std::max(startHeight, targetHeight)));
    }

    g_app.viewAnimationActive = true;
    g_app.viewAnimationStart = GetTickCount64();
    g_app.viewAnimationStartHeight = startHeight;
    g_app.viewAnimationTargetHeight = targetHeight;
    SetWindowPos(hwnd, nullptr, x, y, width, startHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (SetTimer(hwnd, kViewAnimationTimer, 10, nullptr) == 0) {
        StopViewAnimation(hwnd);
        ResizeWindowForView(hwnd);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

int ControlAt(float x, float y) {
    if (PointInRect(kTitleSettingsRect, x, y)) return kSettings;
    if (PointInRect(kMinimizeRect, x, y)) return kMinimize;
    if (PointInRect(kCloseRect, x, y)) return kClose;

    if (g_app.view == MainView::Home) {
        for (int i = 0; i < 2; ++i) {
            if (PointInRect(kTileRects[i], x, y)) return i;
        }
        return kControlNone;
    }

    if (PointInRect(kBackRect, x, y)) return kBack;
    for (int i = 0; i < 3; ++i) {
        if (PointInRect(kThemeRects[i], x, y)) return kThemeSystem + i;
    }
    for (int i = 0; i < 3; ++i) {
        if (PointInRect(kGlassRects[i], x, y)) return kGlassNone + i;
    }
    if (PointInRect(kCloseToTrayRow, x, y)) return kCloseToTrayControl;
    if (PointInRect(kAutoStartRow, x, y)) return kAutoStartControl;
    if (PointInRect(kHotkeyFullscreenButton, x, y)) return kHotkeyFullscreenControl;
    if (PointInRect(kHotkeyRegionButton, x, y)) return kHotkeyRegionControl;
    if (PointInRect(kUpdateButtonRect, x, y)) return kUpdateButton;
    return kControlNone;
}

std::vector<int> FocusOrder() {
    if (g_app.view == MainView::Home) return {kFullScreen, kRegion, kSettings};
    return {kBack, kThemeSystem, kThemeLight, kThemeDark, kGlassNone, kGlassStatic,
            kGlassDynamic, kCloseToTrayControl, kAutoStartControl, kHotkeyFullscreenControl,
            kHotkeyRegionControl, kUpdateButton};
}

void ResetInteraction() {
    g_app.hoverControl = kControlNone;
    g_app.pressedControl = kControlNone;
    g_app.focusControl = kControlNone;
    g_app.keyboardFocus = false;
}

void SwitchView(MainView view) {
    if (g_app.view == view) return;
    CancelHotkeyCapture(true);
    g_app.view = view;
    ResetInteraction();
    if (g_app.hwnd) {
        StartViewAnimation(g_app.hwnd);
        InvalidateRect(g_app.hwnd, nullptr, FALSE);
    }
}

ThemeMode ThemeForControl(int control) {
    if (control == kThemeLight) return ThemeMode::Light;
    if (control == kThemeDark) return ThemeMode::Dark;
    return ThemeMode::System;
}

const wchar_t* ThemeLabel(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light:
            return L"浅色";
        case ThemeMode::Dark:
            return L"深色";
        default:
            return L"跟随系统";
    }
}

void DrawIconButton(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, IconId icon, int control) {
    const bool hovered = g_app.hoverControl == control;
    const bool pressed = g_app.pressedControl == control;
    const bool focused = IsFocused(control);

    if (hovered || pressed || focused) {
        SetBrushColor(g_app.brush.Get(),
                      pressed ? g_app.palette.cardPressed : g_app.palette.cardHover);
        rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 8.0f, 8.0f), g_app.brush.Get());
    }
    if (focused) {
        SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, 8.0f, 8.0f), g_app.brush.Get(), 1.5f);
    }

    const D2D1_RECT_F iconRect =
        D2D1::RectF(rect.left + 7.0f, rect.top + 7.0f, rect.right - 7.0f, rect.bottom - 7.0f);
    const D2D1_COLOR_F color =
        hovered || pressed || focused ? g_app.palette.accent : g_app.palette.secondary;
    DrawIcon(rt, icon, iconRect, color, g_app.brush.Get(), 1.35f);
}

void DrawTile(ID2D1RenderTarget* rt, int index) {
    const D2D1_RECT_F rect = kTileRects[index];
    const bool hovered = g_app.hoverControl == index;
    const bool pressed = g_app.pressedControl == index;
    const bool focused = IsFocused(index);

    D2D1_COLOR_F fill = g_app.palette.card;
    if (pressed) {
        fill = g_app.palette.cardPressed;
    } else if (hovered) {
        fill = g_app.palette.cardHover;
    }
    SetBrushColor(g_app.brush.Get(), fill);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), g_app.brush.Get());

    SetBrushColor(g_app.brush.Get(),
                  hovered || focused ? g_app.palette.borderHover : g_app.palette.border);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), g_app.brush.Get(), 1.0f);

    const D2D1_RECT_F badge = D2D1::RectF(rect.left + 58.0f, rect.top + 24.0f,
                                         rect.left + 110.0f, rect.top + 76.0f);
    SetBrushColor(g_app.brush.Get(),
                  hovered || focused ? g_app.palette.accentSoftHover : g_app.palette.accentSoft);
    rt->FillRoundedRectangle(D2D1::RoundedRect(badge, 14.0f, 14.0f), g_app.brush.Get());

    const IconId icon = index == 0 ? IconId::Screen : IconId::Region;
    DrawIcon(rt, icon,
             D2D1::RectF(badge.left + 12.0f, badge.top + 12.0f, badge.right - 12.0f,
                         badge.bottom - 12.0f),
             g_app.palette.accent, g_app.brush.Get(), 1.6f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    const std::wstring title = index == 0 ? L"全屏识别" : L"框选识别";
    DrawTextEx(rt, g_app.dwrite.Get(), title, g_app.fonts.cardTitle.Get(),
               D2D1::RectF(rect.left + 10.0f, rect.top + 84.0f, rect.right - 10.0f,
                           rect.top + 112.0f),
               g_app.brush.Get(), TextAlign::Center, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    const std::wstring body = index == 0 ? L"扫描二维码与条形码" : L"拖动选择屏幕区域";
    DrawTextEx(rt, g_app.dwrite.Get(), body, g_app.fonts.cardBody.Get(),
               D2D1::RectF(rect.left + 10.0f, rect.top + 116.0f, rect.right - 10.0f,
                           rect.top + 140.0f),
               g_app.brush.Get(), TextAlign::Center, true, true);

    if (focused) {
        const D2D1_RECT_F focusRect =
            D2D1::RectF(rect.left + 2.0f, rect.top + 2.0f, rect.right - 2.0f, rect.bottom - 2.0f);
        SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(focusRect, 10.0f, 10.0f), g_app.brush.Get(),
                                 2.0f);
    }
}

void DrawStatus(ID2D1RenderTarget* rt) {
    if (g_app.statusKind == AppState::StatusKind::None || g_app.status.empty()) return;

    const D2D1_COLOR_F color = g_app.statusKind == AppState::StatusKind::Error
                                   ? g_app.palette.danger
                                   : g_app.palette.secondary;
    SetBrushColor(g_app.brush.Get(), color);
    DrawTextEx(rt, g_app.dwrite.Get(), g_app.status, g_app.fonts.footer.Get(),
               D2D1::RectF(24.0f, 216.0f, 376.0f, 236.0f), g_app.brush.Get(),
               TextAlign::Center, true, true);
}

void DrawAppMark(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect) {
    const float radius = RectWidth(rect) * 0.28f;
    SetBrushColor(g_app.brush.Get(), Rgb(0x0078D4));
    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), g_app.brush.Get());

    const float unit = RectWidth(rect) / 24.0f;
    const float stroke = std::max(1.1f, 1.3f * unit);
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f);
    ComPtr<ID2D1StrokeStyle> strokeStyle;
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(factory.Put());
    if (factory) factory->CreateStrokeStyle(props, nullptr, 0, strokeStyle.Put());

    SetBrushColor(g_app.brush.Get(), D2D1::ColorF(D2D1::ColorF::White));
    auto point = [&](float x, float y) {
        return D2D1::Point2F(rect.left + x * unit, rect.top + y * unit);
    };
    auto line = [&](float x0, float y0, float x1, float y1) {
        rt->DrawLine(point(x0, y0), point(x1, y1), g_app.brush.Get(), stroke,
                     strokeStyle.Get());
    };

    line(5.0f, 12.0f, 5.0f, 6.8f);
    line(5.0f, 6.8f, 10.0f, 6.8f);
    line(14.0f, 6.8f, 19.0f, 6.8f);
    line(19.0f, 6.8f, 19.0f, 12.0f);
    line(5.0f, 12.0f, 5.0f, 17.2f);
    line(5.0f, 17.2f, 10.0f, 17.2f);
    line(14.0f, 17.2f, 19.0f, 17.2f);
    line(19.0f, 17.2f, 19.0f, 12.0f);
    line(8.0f, 12.0f, 16.0f, 12.0f);
}

void DrawCaptionButton(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, int control, bool close) {
    const bool hovered = g_app.hoverControl == control;
    const bool pressed = g_app.pressedControl == control;
    if (hovered || pressed) {
        D2D1_COLOR_F fill = close ? Rgb(0xC42B1C) : g_app.palette.cardHover;
        if (close && pressed) fill = Rgb(0xB32B1C);
        SetBrushColor(g_app.brush.Get(), fill);
        const D2D1_RECT_F hoverRect =
            D2D1::RectF(rect.left + 4.0f, rect.top + 4.0f, rect.right - 4.0f, rect.bottom - 4.0f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(hoverRect, 6.0f, 6.0f), g_app.brush.Get());
    }

    const D2D1_COLOR_F color = close && hovered ? D2D1::ColorF(D2D1::ColorF::White)
                                                : g_app.palette.text;
    SetBrushColor(g_app.brush.Get(), color);
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    if (close) {
        rt->DrawLine(D2D1::Point2F(cx - 5.0f, cy - 5.0f), D2D1::Point2F(cx + 5.0f, cy + 5.0f),
                     g_app.brush.Get(), 1.1f);
        rt->DrawLine(D2D1::Point2F(cx + 5.0f, cy - 5.0f), D2D1::Point2F(cx - 5.0f, cy + 5.0f),
                     g_app.brush.Get(), 1.1f);
    } else {
        rt->DrawLine(D2D1::Point2F(cx - 5.0f, cy), D2D1::Point2F(cx + 5.0f, cy),
                     g_app.brush.Get(), 1.1f);
    }
}

void DrawTitleBar(ID2D1RenderTarget* rt) {
    DrawAppMark(rt, kAppMarkRect);

    if (g_app.view == MainView::Settings) {
        DrawIconButton(rt, kBackRect, IconId::ArrowLeft, kBack);
        SetBrushColor(g_app.brush.Get(), g_app.palette.text);
        DrawTextEx(rt, g_app.dwrite.Get(), L"设置", g_app.fonts.label.Get(), kTitleTextRect,
                   g_app.brush.Get(), TextAlign::Leading, true, true);
    } else {
        SetBrushColor(g_app.brush.Get(), g_app.palette.text);
        DrawTextEx(rt, g_app.dwrite.Get(), L"EasyQR", g_app.fonts.label.Get(),
                   D2D1::RectF(44.0f, 0.0f, 264.0f, kTitleBarHeight), g_app.brush.Get(),
                   TextAlign::Leading, true, true);
    }

    const bool settingsActive = g_app.view == MainView::Settings;
    const bool settingsHovered = g_app.hoverControl == kSettings;
    if (settingsActive || settingsHovered) {
        const D2D1_RECT_F hoverRect = D2D1::RectF(
            kTitleSettingsRect.left + 4.0f, 4.0f, kTitleSettingsRect.right - 4.0f, 40.0f);
        SetBrushColor(g_app.brush.Get(),
                      settingsActive ? g_app.palette.accentSoft : g_app.palette.cardHover);
        rt->FillRoundedRectangle(D2D1::RoundedRect(hoverRect, 7.0f, 7.0f), g_app.brush.Get());
    }
    DrawIcon(rt, IconId::Settings,
             D2D1::RectF(kTitleSettingsRect.left + 13.0f, 13.0f, kTitleSettingsRect.right - 13.0f,
                         31.0f),
             settingsActive ? g_app.palette.accent : g_app.palette.secondary, g_app.brush.Get(),
             1.35f);
    DrawCaptionButton(rt, kMinimizeRect, kMinimize, false);
    DrawCaptionButton(rt, kCloseRect, kClose, true);
}

void DrawHome(ID2D1RenderTarget* rt) {
    for (int i = 0; i < 2; ++i) DrawTile(rt, i);
    DrawStatus(rt);
}

void DrawThemeButton(ID2D1RenderTarget* rt, int index) {
    const int control = kThemeSystem + index;
    const D2D1_RECT_F rect = kThemeRects[index];
    const ThemeMode mode = static_cast<ThemeMode>(index);
    const bool selected = g_app.themeMode == mode;
    const bool hovered = g_app.hoverControl == control;
    const bool pressed = g_app.pressedControl == control;
    const bool focused = IsFocused(control);

    D2D1_COLOR_F fill = g_app.palette.card;
    if (selected) {
        fill = g_app.palette.accentSoftHover;
    } else if (pressed) {
        fill = g_app.palette.cardPressed;
    } else if (hovered) {
        fill = g_app.palette.cardHover;
    }
    SetBrushColor(g_app.brush.Get(), fill);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 9.0f, 9.0f), g_app.brush.Get());

    SetBrushColor(g_app.brush.Get(),
                  selected || focused ? g_app.palette.accent
                                      : (hovered ? g_app.palette.borderHover
                                                 : g_app.palette.border));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, 9.0f, 9.0f), g_app.brush.Get(),
                             selected || focused ? 1.4f : 1.0f);

    SetBrushColor(g_app.brush.Get(),
                  selected || hovered || focused ? g_app.palette.accent : g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), ThemeLabel(mode), g_app.fonts.chip.Get(),
               D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), g_app.brush.Get(),
               TextAlign::Center, true, true);

    if (focused) {
        const D2D1_RECT_F focusRect =
            D2D1::RectF(rect.left + 2.0f, rect.top + 2.0f, rect.right - 2.0f, rect.bottom - 2.0f);
        SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(focusRect, 7.0f, 7.0f), g_app.brush.Get(),
                                 1.4f);
    }
}

const wchar_t* GlassLabel(GlassMode mode) {
    switch (mode) {
        case GlassMode::None:
            return L"无模糊";
        case GlassMode::Dynamic:
            return L"动态模糊";
        default:
            return L"静态模糊";
    }
}

void DrawGlassButton(ID2D1RenderTarget* rt, int index) {
    const int control = kGlassNone + index;
    const D2D1_RECT_F rect = kGlassRects[index];
    const GlassMode mode = static_cast<GlassMode>(index);
    const bool selected = g_app.glassMode == mode;
    const bool hovered = g_app.hoverControl == control;
    const bool pressed = g_app.pressedControl == control;
    const bool focused = IsFocused(control);

    D2D1_COLOR_F fill = g_app.palette.card;
    if (selected) {
        fill = g_app.palette.accentSoftHover;
    } else if (pressed) {
        fill = g_app.palette.cardPressed;
    } else if (hovered) {
        fill = g_app.palette.cardHover;
    }
    SetBrushColor(g_app.brush.Get(), fill);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 9.0f, 9.0f), g_app.brush.Get());

    SetBrushColor(g_app.brush.Get(),
                  selected || focused ? g_app.palette.accent
                                      : (hovered ? g_app.palette.borderHover
                                                 : g_app.palette.border));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, 9.0f, 9.0f), g_app.brush.Get(),
                             selected || focused ? 1.4f : 1.0f);

    SetBrushColor(g_app.brush.Get(),
                  selected || hovered || focused ? g_app.palette.accent : g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), GlassLabel(mode), g_app.fonts.chip.Get(),
               D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), g_app.brush.Get(),
               TextAlign::Center, true, true);

    if (focused) {
        const D2D1_RECT_F focusRect =
            D2D1::RectF(rect.left + 2.0f, rect.top + 2.0f, rect.right - 2.0f, rect.bottom - 2.0f);
        SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(focusRect, 7.0f, 7.0f), g_app.brush.Get(),
                                 1.4f);
    }
}

void FillRoundedRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, float radius,
                     const D2D1_COLOR_F& color) {
    SetBrushColor(g_app.brush.Get(), color);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), g_app.brush.Get());
}

void StrokeRoundedRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, float radius,
                       const D2D1_COLOR_F& color, float width) {
    SetBrushColor(g_app.brush.Get(), color);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), g_app.brush.Get(), width);
}

void DrawSwitch(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, bool on, bool hovered,
                bool focused, bool pressed) {
    const float radius = RectHeight(rect) * 0.5f;
    D2D1_COLOR_F track = on ? Rgb(0x0067C0) : g_app.palette.card;
    D2D1_COLOR_F border = on ? Rgb(0x0067C0) : g_app.palette.border;
    if (!on && pressed) {
        track = g_app.palette.cardPressed;
    } else if (!on && hovered) {
        track = g_app.palette.cardHover;
        border = g_app.palette.borderHover;
    } else if (on && (hovered || pressed)) {
        track = Rgb(0x0078D4);
        border = Rgb(0x0078D4);
    }
    FillRoundedRect(rt, rect, radius, track);
    SetBrushColor(g_app.brush.Get(), border);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), g_app.brush.Get(), 1.0f);

    if (focused) {
        const D2D1_RECT_F focusRect =
            D2D1::RectF(rect.left - 2.0f, rect.top - 2.0f, rect.right + 2.0f, rect.bottom + 2.0f);
        SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(focusRect, radius + 2.0f, radius + 2.0f),
                                 g_app.brush.Get(), 1.4f);
    }

    const float knobRadius = RectHeight(rect) * 0.5f - 3.0f;
    const float knobX = on ? rect.right - 3.0f - knobRadius : rect.left + 3.0f + knobRadius;
    const float centerY = (rect.top + rect.bottom) * 0.5f;
    SetBrushColor(g_app.brush.Get(), on ? Rgb(0xFFFFFF) : g_app.palette.tertiary);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, centerY), knobRadius, knobRadius),
                    g_app.brush.Get());
}

void DrawToggleRow(ID2D1RenderTarget* rt, const D2D1_RECT_F& row, const D2D1_RECT_F& sw,
                   const std::wstring& label, int control, bool value) {
    const bool hovered = g_app.hoverControl == control;
    const bool pressed = g_app.pressedControl == control;
    const bool focused = IsFocused(control);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), label, g_app.fonts.cardBody.Get(),
               D2D1::RectF(row.left, row.top, sw.left - 10.0f, row.bottom), g_app.brush.Get(),
               TextAlign::Leading, true, true);
    DrawSwitch(rt, sw, value, hovered, focused, pressed);
}

void DrawHotkeyButton(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, const std::wstring& label,
                      int control, bool capturing, bool disabled) {
    const bool hovered = g_app.hoverControl == control;
    const bool pressed = g_app.pressedControl == control;
    const bool focused = IsFocused(control);

    D2D1_COLOR_F fill = g_app.palette.card;
    if (capturing) {
        fill = g_app.palette.accentSoftHover;
    } else if (pressed) {
        fill = g_app.palette.cardPressed;
    } else if (hovered || focused) {
        fill = g_app.palette.cardHover;
    }
    FillRoundedRect(rt, rect, 9.0f, fill);

    D2D1_COLOR_F border = g_app.palette.border;
    if (capturing || focused) {
        border = g_app.palette.accent;
    } else if (hovered) {
        border = g_app.palette.borderHover;
    }
    StrokeRoundedRect(rt, rect, 9.0f, border, capturing || focused ? 1.4f : 1.0f);

    D2D1_COLOR_F text = disabled ? g_app.palette.tertiary
                                 : (capturing || hovered || focused ? g_app.palette.accent
                                                                    : g_app.palette.text);
    SetBrushColor(g_app.brush.Get(), text);
    DrawTextEx(rt, g_app.dwrite.Get(), label, g_app.fonts.chip.Get(), rect, g_app.brush.Get(),
               TextAlign::Center, true, true);
}

void DrawUpdateButton(ID2D1RenderTarget* rt) {
    const bool busy = g_app.updateState == UpdateState::Checking ||
                      g_app.updateState == UpdateState::Downloading ||
                      g_app.updateState == UpdateState::Launching;
    const bool hovered = g_app.hoverControl == kUpdateButton;
    const bool pressed = g_app.pressedControl == kUpdateButton;

    std::wstring label = L"检查更新";
    switch (g_app.updateState) {
        case UpdateState::Checking:
            label = L"正在检查…";
            break;
        case UpdateState::Downloading:
            label = L"正在下载…";
            break;
        case UpdateState::Launching:
            label = L"正在启动安装程序…";
            break;
        case UpdateState::Available:
            label = L"更新到 v" + g_app.updateVersion;
            break;
        case UpdateState::UpToDate:
            label = L"重新检查";
            break;
        case UpdateState::Error:
            label = L"重试";
            break;
        default:
            break;
    }

    const D2D1_COLOR_F accent = g_app.dark ? g_app.palette.accent : Rgb(0x0067C0);
    D2D1_COLOR_F fill = busy ? g_app.palette.card : accent;
    if (!busy && pressed) {
        fill = g_app.dark ? Rgb(0x4CC2FF) : Rgb(0x005A9E);
    } else if (!busy && hovered) {
        fill = g_app.dark ? Rgb(0x86D6FF) : Rgb(0x106EBE);
    }
    SetBrushColor(g_app.brush.Get(), fill);
    rt->FillRoundedRectangle(D2D1::RoundedRect(kUpdateButtonRect, 8.0f, 8.0f),
                             g_app.brush.Get());

    SetBrushColor(g_app.brush.Get(),
                  busy ? g_app.palette.tertiary
                       : (g_app.dark ? Rgb(0x0A0A0A)
                                     : D2D1::ColorF(D2D1::ColorF::White)));
    DrawTextEx(rt, g_app.dwrite.Get(), label, g_app.fonts.chip.Get(), kUpdateButtonRect,
               g_app.brush.Get(), TextAlign::Center, true, true);
}

void DrawSettings(ID2D1RenderTarget* rt) {
    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 56.0f), D2D1::Point2F(kClientWidth - 24.0f, 56.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"外观与模糊", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 64.0f, 200.0f, 80.0f), g_app.brush.Get(), TextAlign::Leading,
               true);
    for (int i = 0; i < 3; ++i) DrawThemeButton(rt, i);
    for (int i = 0; i < 3; ++i) DrawGlassButton(rt, i);

    std::wstring glassNote = L"静态在拖动时重新采样 · 动态使用系统实时 Acrylic";
    D2D1_COLOR_F glassNoteColor = g_app.palette.tertiary;
    if (g_app.glassMode == GlassMode::Dynamic && !g_app.dynamicBackdrop) {
        glassNote = L"当前系统不支持动态 Acrylic，已回退到静态采样";
        glassNoteColor = g_app.palette.secondary;
    }
    SetBrushColor(g_app.brush.Get(), glassNoteColor);
    DrawTextEx(rt, g_app.dwrite.Get(), glassNote, g_app.fonts.chipSmall.Get(),
               D2D1::RectF(24.0f, 160.0f, 376.0f, 176.0f), g_app.brush.Get(),
               TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 182.0f), D2D1::Point2F(kClientWidth - 24.0f, 182.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"常规", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 190.0f, 200.0f, 208.0f), g_app.brush.Get(), TextAlign::Leading,
               true);
    DrawToggleRow(rt, kCloseToTrayRow, kCloseToTraySwitch, L"关闭窗口时最小化到托盘",
                  kCloseToTrayControl, g_app.closeToTray);
    DrawToggleRow(rt, kAutoStartRow, kAutoStartSwitch, L"开机自启动（仅托盘）",
                  kAutoStartControl, g_app.autoStart);

    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 290.0f), D2D1::Point2F(kClientWidth - 24.0f, 290.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"快捷键", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 298.0f, 200.0f, 316.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), L"全屏识别", g_app.fonts.cardBody.Get(),
               D2D1::RectF(24.0f, kHotkeyFullscreenButton.top, kHotkeyFullscreenButton.left - 12.0f,
                           kHotkeyFullscreenButton.bottom),
               g_app.brush.Get(), TextAlign::Leading, true, true);
    DrawTextEx(rt, g_app.dwrite.Get(), L"框选识别", g_app.fonts.cardBody.Get(),
               D2D1::RectF(24.0f, kHotkeyRegionButton.top, kHotkeyRegionButton.left - 12.0f,
                           kHotkeyRegionButton.bottom),
               g_app.brush.Get(), TextAlign::Leading, true, true);

    const bool capturingFullscreen = g_app.hotkeyCapture == AppState::HotkeyCapture::Fullscreen;
    const bool capturingRegion = g_app.hotkeyCapture == AppState::HotkeyCapture::Region;
    const bool captureActive = g_app.hotkeyCapture != AppState::HotkeyCapture::None;
    DrawHotkeyButton(rt, kHotkeyFullscreenButton,
                     capturingFullscreen ? L"请按下组合键…" : g_app.fullscreenHotkey.Label(),
                     kHotkeyFullscreenControl, capturingFullscreen, capturingRegion);
    DrawHotkeyButton(rt, kHotkeyRegionButton,
                     capturingRegion ? L"请按下组合键…" : g_app.regionHotkey.Label(),
                     kHotkeyRegionControl, capturingRegion, capturingFullscreen);

    std::wstring note = L"快捷键需包含 Ctrl/Alt/Shift/Win，或使用 F1–F24";
    D2D1_COLOR_F noteColor = g_app.palette.tertiary;
    if (!g_app.settingsNotice.empty()) {
        note = g_app.settingsNotice;
        noteColor = g_app.settingsNoticeError ? g_app.palette.danger : g_app.palette.secondary;
    } else if (captureActive) {
        note = L"请按下新的组合键，Esc 取消";
        noteColor = g_app.palette.accent;
    }
    SetBrushColor(g_app.brush.Get(), noteColor);
    DrawTextEx(rt, g_app.dwrite.Get(), note, g_app.fonts.chipSmall.Get(),
               D2D1::RectF(24.0f, 398.0f, 376.0f, 416.0f), g_app.brush.Get(),
               TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 424.0f), D2D1::Point2F(kClientWidth - 24.0f, 424.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"关于与更新", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 432.0f, 200.0f, 450.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), L"EasyQR " EASYQR_VERSION_STRING, g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 458.0f, 220.0f, 478.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    DrawUpdateButton(rt);

    std::wstring updateText = L"从 GitHub Releases 检查新版本";
    D2D1_COLOR_F updateColor = g_app.palette.tertiary;
    if (!g_app.updateMessage.empty()) {
        updateText = g_app.updateMessage;
        updateColor = g_app.updateState == UpdateState::Error
                          ? g_app.palette.danger
                          : (g_app.updateState == UpdateState::Available ? g_app.palette.accent
                                                                         : g_app.palette.secondary);
    }
    SetBrushColor(g_app.brush.Get(), updateColor);
    DrawTextEx(rt, g_app.dwrite.Get(), updateText, g_app.fonts.chipSmall.Get(),
               D2D1::RectF(24.0f, 490.0f, 376.0f, 508.0f), g_app.brush.Get(),
               TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"图像与扫描结果仅在本机内存中处理，不作保存。",
               g_app.fonts.footer.Get(), D2D1::RectF(24.0f, 516.0f, 376.0f, 532.0f),
               g_app.brush.Get(), TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.tertiary);
    DrawTextEx(rt, g_app.dwrite.Get(),
               L"MIT License · ZXing-C++ + quirc · 设置卸载时删除",
               g_app.fonts.chipSmall.Get(), D2D1::RectF(24.0f, 534.0f, 376.0f, 550.0f),
               g_app.brush.Get(), TextAlign::Leading, true, true);
}

void PaintMainWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        PAINTSTRUCT hiddenPaint{};
        BeginPaint(hwnd, &hiddenPaint);
        EndPaint(hwnd, &hiddenPaint);
        return;
    }

    EnsureRenderTarget(hwnd);
    if (!g_app.rt) {
        static bool errorShown = false;
        if (!errorShown) {
            errorShown = true;
            MessageBoxW(hwnd, L"无法初始化图形渲染，EasyQR 将退出。", EASYQR_APP_NAME,
                        MB_ICONERROR | MB_OK);
            PostMessageW(hwnd, kQuitForInstallerMessage, 0, 0);
        }
        return;
    }

    PAINTSTRUCT ps{};
    BeginPaint(hwnd, &ps);
    ID2D1RenderTarget* rt = g_app.rt.Get();
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->SetDpi(static_cast<float>(g_app.dpi), static_cast<float>(g_app.dpi));
    const bool liveBackdrop = g_app.compositionMode &&
                              g_app.glassMode == GlassMode::Dynamic && g_app.dynamicBackdrop;
    if (GlassEnabled() && liveBackdrop) {
        rt->Clear(D2D1::ColorF(0, 0.0f));
        SetBrushColor(g_app.brush.Get(), g_app.palette.page);
        RECT client{};
        GetClientRect(hwnd, &client);
        rt->FillRectangle(
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(client.right),
                        static_cast<float>(client.bottom)),
            g_app.brush.Get());
    } else if (GlassEnabled() && g_app.glassBitmap) {
        rt->Clear(g_app.palette.page);
        DrawGlassBackground(rt, hwnd);
    } else if (GlassEnabled() && g_app.backdropEnabled) {
        rt->Clear(D2D1::ColorF(0, 0.0f));
        SetBrushColor(g_app.brush.Get(), g_app.palette.page);
        RECT client{};
        GetClientRect(hwnd, &client);
        rt->FillRectangle(
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(client.right),
                        static_cast<float>(client.bottom)),
            g_app.brush.Get());
    } else {
        rt->Clear(g_app.palette.page);
    }

    DrawTitleBar(rt);
    if (g_app.view == MainView::Home) {
        DrawHome(rt);
    } else {
        DrawSettings(rt);
    }

    const HRESULT hr = rt->EndDraw();
    if (SUCCEEDED(hr) && g_app.compositionMode && g_app.swapChain) {
        g_app.swapChain->Present(0, 0);
    }
    EndPaint(hwnd, &ps);

    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        DiscardRenderTarget();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void CenterMainWindow(HWND hwnd, int width, int height) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ActivateControl(int control) {
    if (g_app.hotkeyCapture != AppState::HotkeyCapture::None) {
        const bool activeButton =
            (g_app.hotkeyCapture == AppState::HotkeyCapture::Fullscreen &&
             control == kHotkeyFullscreenControl) ||
            (g_app.hotkeyCapture == AppState::HotkeyCapture::Region &&
             control == kHotkeyRegionControl);
        CancelHotkeyCapture(true);
        if (activeButton) return;
    }

    if (control == kFullScreen || control == kRegion) {
        if (g_app.view != MainView::Home) return;
        SetStatus(L"", AppState::StatusKind::None);
        StartScan(control == kFullScreen ? ScanMode::Fullscreen : ScanMode::Region);
        return;
    }
    if (control == kSettings) {
        SwitchView(MainView::Settings);
        return;
    }
    if (control == kMinimize) {
        if (g_app.hwnd) {
            CompleteViewAnimation(g_app.hwnd);
            ShowWindow(g_app.hwnd, SW_MINIMIZE);
        }
        return;
    }
    if (control == kClose) {
        if (g_app.hwnd) PostMessageW(g_app.hwnd, WM_CLOSE, 0, 0);
        return;
    }
    if (control == kBack) {
        SwitchView(MainView::Home);
        return;
    }
    if (control == kUpdateButton) {
        BeginUpdateAction(g_app.hwnd);
        return;
    }
    if (control >= kThemeSystem && control <= kThemeDark) {
        SetThemeMode(ThemeForControl(control));
        g_app.focusControl = control;
        return;
    }
    if (control >= kGlassNone && control <= kGlassDynamic) {
        SetGlassMode(static_cast<GlassMode>(control - kGlassNone));
        g_app.focusControl = control;
        return;
    }
    if (control == kCloseToTrayControl) {
        SetCloseToTrayEnabled(!g_app.closeToTray);
        return;
    }
    if (control == kAutoStartControl) {
        ToggleAutoStart();
        return;
    }
    if (control == kHotkeyFullscreenControl) {
        BeginHotkeyCapture(true);
        return;
    }
    if (control == kHotkeyRegionControl) {
        BeginHotkeyCapture(false);
        return;
    }
}

NOTIFYICONDATAW g_trayData{};

template <size_t N>
void CopyTrayText(wchar_t (&destination)[N], const std::wstring& text) {
    const size_t count = std::min(text.size(), N - 1);
    std::wmemcpy(destination, text.c_str(), count);
    destination[count] = L'\0';
}

bool EnsureTrayIcon(HWND hwnd) {
    if (!hwnd) return false;
    if (g_app.trayVisible) return true;

    ZeroMemory(&g_trayData, sizeof(g_trayData));
    g_trayData.cbSize = sizeof(g_trayData);
    g_trayData.hWnd = hwnd;
    g_trayData.uID = 1;
    g_trayData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayData.uCallbackMessage = kTrayCallbackMessage;
    const UINT trayDpi = GetDpiForSystem();
    const int trayIconWidth = GetSystemMetricsForDpi(SM_CXSMICON, trayDpi);
    const int trayIconHeight = GetSystemMetricsForDpi(SM_CYSMICON, trayDpi);
    g_trayData.hIcon = static_cast<HICON>(LoadImageW(
        g_app.instance, MAKEINTRESOURCEW(IDI_TRAY), IMAGE_ICON, trayIconWidth, trayIconHeight,
        LR_SHARED));
    if (!g_trayData.hIcon) {
        g_trayData.hIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_TRAY));
    }
    CopyTrayText(g_trayData.szTip, L"EasyQR");

    if (!Shell_NotifyIconW(NIM_ADD, &g_trayData)) {
        g_app.trayVisible = false;
        return false;
    }
    g_app.trayVisible = true;
    return true;
}

void RemoveTrayIcon() {
    if (!g_app.trayVisible) return;
    Shell_NotifyIconW(NIM_DELETE, &g_trayData);
    g_app.trayVisible = false;
}

void ShowTrayBalloon(const std::wstring& title, const std::wstring& text, bool error) {
    if (!g_app.trayVisible) return;
    NOTIFYICONDATAW data = g_trayData;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    data.uTimeout = 4000;
    CopyTrayText(data.szInfoTitle, title);
    CopyTrayText(data.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void UpdateTrayPresence(HWND hwnd) {
    if (!hwnd) return;
    const bool hidden = !IsWindowVisible(hwnd);
    if (hidden || g_app.closeToTray || g_app.launchedHidden) {
        EnsureTrayIcon(hwnd);
    } else {
        RemoveTrayIcon();
    }
}

void ApplyWindowIcons(HWND hwnd) {
    if (!hwnd) return;
    const UINT dpi = DpiOfWindow(hwnd);
    const int largeWidth = GetSystemMetricsForDpi(SM_CXICON, dpi);
    const int largeHeight = GetSystemMetricsForDpi(SM_CYICON, dpi);
    const int smallWidth = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const int smallHeight = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
    const HICON large = static_cast<HICON>(
        LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, largeWidth,
                   largeHeight, LR_SHARED));
    const HICON small = static_cast<HICON>(
        LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, smallWidth,
                   smallHeight, LR_SHARED));
    if (large) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(large));
    }
    if (small) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(small));
    }
}

void ShowMainFromTray(HWND hwnd) {
    if (!hwnd) return;
    g_app.launchedHidden = false;
    RefreshMainBackdrop();
    ShowWindow(hwnd, SW_RESTORE);
    ApplyWindowIcons(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
    UpdateTrayPresence(hwnd);
}

void HideToTray(HWND hwnd) {
    CancelHotkeyCapture(true);
    CompleteViewAnimation(hwnd);
    if (!EnsureTrayIcon(hwnd)) {
        MessageBoxW(hwnd, L"无法创建托盘图标，EasyQR 将保持主窗口打开。", L"EasyQR",
                    MB_ICONERROR | MB_OK);
        return;
    }
    ShowWindow(hwnd, SW_HIDE);
    DiscardRenderTarget();
    UpdateTrayPresence(hwnd);
    if (!g_app.trayBalloonShown) {
        g_app.trayBalloonShown = true;
        std::wstring hint = L"按 " + g_app.fullscreenHotkey.Label() + L" / " +
                            g_app.regionHotkey.Label() + L" 快速识别屏幕。";
        ShowTrayBalloon(L"EasyQR 仍在后台运行", hint, false);
    }
}

void QuitApplication(HWND hwnd) {
    g_app.quitting = true;
    UnregisterConfiguredHotkeys();
    RemoveTrayIcon();
    if (hwnd) DestroyWindow(hwnd);
}

void UnregisterConfiguredHotkeys() {
    if (g_app.hwnd) {
        UnregisterHotKey(g_app.hwnd, kHotkeyFullscreenId);
        UnregisterHotKey(g_app.hwnd, kHotkeyRegionId);
    }
    g_app.hotkeyFullscreenRegistered = false;
    g_app.hotkeyRegionRegistered = false;
}

void RegisterCurrentHotkeys(bool showErrors) {
    if (!g_app.hwnd) return;
    UnregisterConfiguredHotkeys();
    g_app.hotkeyFullscreenRegistered =
        RegisterHotKey(g_app.hwnd, kHotkeyFullscreenId,
                       g_app.fullscreenHotkey.mods | MOD_NOREPEAT, g_app.fullscreenHotkey.vk) != FALSE;
    g_app.hotkeyRegionRegistered =
        RegisterHotKey(g_app.hwnd, kHotkeyRegionId, g_app.regionHotkey.mods | MOD_NOREPEAT,
                       g_app.regionHotkey.vk) != FALSE;

    if (!g_app.hotkeyFullscreenRegistered || !g_app.hotkeyRegionRegistered) {
        if (showErrors) {
            g_app.settingsNotice = L"部分全局快捷键注册失败，可能已被其他程序占用";
            g_app.settingsNoticeError = true;
        }
    } else if (showErrors && g_app.settingsNoticeError) {
        g_app.settingsNotice.clear();
        g_app.settingsNoticeError = false;
    }
}

bool ApplyHotkeyBindings(const HotkeyBinding& fullscreen, const HotkeyBinding& region) {
    if (!g_app.hwnd) return false;
    const HotkeyBinding oldFullscreen = g_app.fullscreenHotkey;
    const HotkeyBinding oldRegion = g_app.regionHotkey;

    UnregisterConfiguredHotkeys();
    bool fullscreenOk = RegisterHotKey(g_app.hwnd, kHotkeyFullscreenId,
                                       fullscreen.mods | MOD_NOREPEAT,
                                       fullscreen.vk) != FALSE;
    bool regionOk = false;
    if (fullscreenOk) {
        regionOk = RegisterHotKey(g_app.hwnd, kHotkeyRegionId, region.mods | MOD_NOREPEAT,
                                  region.vk) != FALSE;
    }

    if (!fullscreenOk || !regionOk) {
        if (fullscreenOk) UnregisterHotKey(g_app.hwnd, kHotkeyFullscreenId);
        g_app.hotkeyFullscreenRegistered =
            RegisterHotKey(g_app.hwnd, kHotkeyFullscreenId,
                           oldFullscreen.mods | MOD_NOREPEAT, oldFullscreen.vk) != FALSE;
        g_app.hotkeyRegionRegistered =
            RegisterHotKey(g_app.hwnd, kHotkeyRegionId, oldRegion.mods | MOD_NOREPEAT,
                           oldRegion.vk) != FALSE;
        return false;
    }

    g_app.fullscreenHotkey = fullscreen;
    g_app.regionHotkey = region;
    g_app.hotkeyFullscreenRegistered = true;
    g_app.hotkeyRegionRegistered = true;
    SaveHotkey(true, fullscreen);
    SaveHotkey(false, region);
    return true;
}

void BeginHotkeyCapture(bool fullscreen) {
    g_app.hotkeyCapture = fullscreen ? AppState::HotkeyCapture::Fullscreen
                                     : AppState::HotkeyCapture::Region;
    g_app.settingsNotice.clear();
    g_app.settingsNoticeError = false;
    UnregisterConfiguredHotkeys();
    g_app.focusControl = fullscreen ? kHotkeyFullscreenControl : kHotkeyRegionControl;
    g_app.keyboardFocus = true;
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void CancelHotkeyCapture(bool restoreHotkeys) {
    if (g_app.hotkeyCapture == AppState::HotkeyCapture::None) return;
    g_app.hotkeyCapture = AppState::HotkeyCapture::None;
    g_app.settingsNotice.clear();
    g_app.settingsNoticeError = false;
    if (restoreHotkeys) RegisterCurrentHotkeys(true);
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void HandleHotkeyCaptureKey(WPARAM key) {
    if (key == VK_ESCAPE) {
        CancelHotkeyCapture(true);
        return;
    }
    if (IsModifierKey(static_cast<UINT>(key))) return;

    if (!IsSupportedHotkey(static_cast<UINT>(key))) {
        g_app.settingsNotice = L"不支持此按键，请使用字母、数字、F1–F24 或常用导航键组合";
        g_app.settingsNoticeError = true;
        if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
        return;
    }

    UINT mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;

    HotkeyBinding candidate;
    candidate.mods = mods;
    candidate.vk = static_cast<UINT>(key);
    if (!candidate.IsValid()) {
        g_app.settingsNotice = L"快捷键需包含 Ctrl/Alt/Shift/Win，或使用 F1–F24";
        g_app.settingsNoticeError = true;
        if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
        return;
    }

    const bool capturingFullscreen =
        g_app.hotkeyCapture == AppState::HotkeyCapture::Fullscreen;
    const HotkeyBinding& other = capturingFullscreen ? g_app.regionHotkey
                                                     : g_app.fullscreenHotkey;
    if (candidate.mods == other.mods && candidate.vk == other.vk) {
        g_app.settingsNotice = L"全屏识别和框选识别不能使用同一组快捷键";
        g_app.settingsNoticeError = true;
        if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
        return;
    }

    const bool applied = capturingFullscreen
                             ? ApplyHotkeyBindings(candidate, g_app.regionHotkey)
                             : ApplyHotkeyBindings(g_app.fullscreenHotkey, candidate);
    g_app.hotkeyCapture = AppState::HotkeyCapture::None;
    if (applied) {
        g_app.settingsNotice = L"快捷键已更新";
        g_app.settingsNoticeError = false;
    } else {
        g_app.settingsNotice = L"快捷键注册失败，可能已被其他程序占用，已恢复原快捷键";
        g_app.settingsNoticeError = true;
    }
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void SetCloseToTrayEnabled(bool enabled) {
    if (enabled && !EnsureTrayIcon(g_app.hwnd)) {
        MessageBoxW(g_app.hwnd, L"无法创建托盘图标，后台常驻未启用。", L"EasyQR",
                    MB_ICONERROR | MB_OK);
        return;
    }
    g_app.closeToTray = enabled;
    SaveCloseToTray(enabled);
    UpdateTrayPresence(g_app.hwnd);
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void ToggleAutoStart() {
    const bool wanted = !g_app.autoStart;
    if (!SetAutoStart(wanted)) {
        MessageBoxW(g_app.hwnd, L"无法更新开机自启动设置，请检查注册表权限。", L"EasyQR",
                    MB_ICONERROR | MB_OK);
        return;
    }
    g_app.autoStart = wanted;
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

bool CaptureStaticBackdropWithHiddenWindow(HWND hwnd) {
    const bool wasVisible = hwnd && IsWindowVisible(hwnd) && !IsIconic(hwnd);
    if (wasVisible) {
        ShowWindow(hwnd, SW_HIDE);
        DwmFlush();
        Sleep(40);
    }
    const bool captured = CaptureMainBackdropNow();
    if (wasVisible) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        UpdateWindow(hwnd);
    }
    return captured;
}

void SetGlassMode(GlassMode mode) {
    if (g_app.glassMode == mode) return;
    HWND hwnd = g_app.hwnd;
    if (hwnd && g_app.viewAnimationActive) CompleteViewAnimation(hwnd);

    g_app.glassMode = mode;
    SaveGlassMode(mode);

    if (mode == GlassMode::None) {
        g_app.glassBitmap.Reset();
        g_app.glassAvailable = false;
        g_app.glassPixels.clear();
        if (hwnd) DisableSystemBackdrop(hwnd);
        UpdatePalette();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (mode == GlassMode::Dynamic) {
        g_app.glassBitmap.Reset();
        g_app.glassAvailable = false;
        g_app.glassPixels.clear();
        if (hwnd) {
            EnsureRenderTarget(hwnd);
            if (EnableDynamicBackdrop(hwnd)) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            // Live Acrylic is unavailable (older build or compositor failure);
            // keep the mode selected but use the sampled backdrop instead.
            if (!CaptureStaticBackdropWithHiddenWindow(hwnd) && g_app.compositionMode &&
                g_app.rt) {
                EnableStaticBackdropFallback(hwnd);
            }
        }
        UpdatePalette();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (hwnd) DisableSystemBackdrop(hwnd);
    g_app.glassBitmap.Reset();
    g_app.glassAvailable = false;
    if (!g_app.glassPixels.empty()) {
        if (g_app.rt) {
            CreateGlassBitmap();
        } else {
            g_app.glassAvailable = true;
        }
    } else if (hwnd) {
        if (!CaptureStaticBackdropWithHiddenWindow(hwnd) && g_app.compositionMode && g_app.rt) {
            EnableStaticBackdropFallback(hwnd);
        }
    }
    UpdatePalette();
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void JoinUpdateThread() {
    if (g_app.updateThread.joinable()) g_app.updateThread.join();
}

void StartUpdateCheck(HWND hwnd) {
    JoinUpdateThread();
    g_app.updateState = UpdateState::Checking;
    g_app.updateMessage = L"正在从 GitHub 检查更新…";
    g_app.updateVersion.clear();
    g_app.updateTag.clear();
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    g_app.updateThread = std::thread([] {
        auto* result = new UpdateCheckResult(CheckForUpdate(EASYQR_VERSION_STRING));
        if (g_app.hwnd) {
            PostMessageW(g_app.hwnd, kUpdateCheckDoneMessage, 0,
                         reinterpret_cast<LPARAM>(result));
        } else {
            delete result;
        }
    });
}

void StartUpdateDownload(HWND hwnd) {
    if (g_app.updateVersion.empty() || g_app.updateTag.empty()) return;
    JoinUpdateThread();
    const std::wstring version = g_app.updateVersion;
    const std::wstring tag = g_app.updateTag;
    g_app.updateState = UpdateState::Downloading;
    g_app.updateMessage = L"正在下载更新，请稍候…";
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    g_app.updateThread = std::thread([version, tag] {
        auto* result = new UpdatePrepareResult(PrepareUpdate(version, tag));
        if (g_app.hwnd) {
            PostMessageW(g_app.hwnd, kUpdateReadyMessage, 0, reinterpret_cast<LPARAM>(result));
        } else {
            delete result;
        }
    });
}

void BeginUpdateAction(HWND hwnd) {
    if (g_app.updateState == UpdateState::Checking ||
        g_app.updateState == UpdateState::Downloading ||
        g_app.updateState == UpdateState::Launching) {
        return;
    }
    if (g_app.updateState == UpdateState::Available) {
        StartUpdateDownload(hwnd);
    } else {
        StartUpdateCheck(hwnd);
    }
}

void HandleUpdateCheckResult(HWND hwnd, UpdateCheckResult* result) {
    JoinUpdateThread();
    std::unique_ptr<UpdateCheckResult> holder(result);
    if (!result) return;
    if (!result->ok) {
        g_app.updateState = UpdateState::Error;
        g_app.updateMessage = result->error;
    } else if (result->available) {
        g_app.updateState = UpdateState::Available;
        g_app.updateVersion = result->version;
        g_app.updateTag = result->tag;
        g_app.updateMessage = L"发现新版本 v" + result->version + L"，点击按钮自动更新";
    } else {
        g_app.updateState = UpdateState::UpToDate;
        g_app.updateMessage = L"已是最新版本";
        g_app.updateVersion.clear();
        g_app.updateTag.clear();
    }
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void HandleUpdateReadyResult(HWND hwnd, UpdatePrepareResult* result) {
    JoinUpdateThread();
    std::unique_ptr<UpdatePrepareResult> holder(result);
    if (!result) return;
    if (!result->ok) {
        g_app.updateState = UpdateState::Error;
        g_app.updateMessage = result->error;
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    std::wstring error;
    if (result->installer) {
        SavePendingUpdateVersion(g_app.updateVersion);
        if (StartInstaller(result->filePath, &error)) {
            g_app.updateState = UpdateState::Launching;
            g_app.updateMessage = L"等待管理员授权，完成后会自动重新打开";
            if (hwnd) {
                SetTimer(hwnd, kUpdateLaunchTimer, kUpdateLaunchTimeoutMs, nullptr);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        }
        ClearPendingUpdateVersion();
    } else {
        SavePendingUpdateVersion(g_app.updateVersion);
        if (ApplyPortableUpdate(result->filePath, &error)) {
            QuitApplication(hwnd);
            return;
        }
        ClearPendingUpdateVersion();
    }
    g_app.updateState = UpdateState::Error;
    g_app.updateMessage = error;
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void ShowTrayMenu(HWND hwnd) {
    if (!hwnd) return;
    g_app.autoStart = IsAutoStartEnabled();

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kTrayOpen, L"打开 EasyQR");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring fullscreenItem = L"全屏识别\t" + g_app.fullscreenHotkey.Label();
    const std::wstring regionItem = L"框选识别\t" + g_app.regionHotkey.Label();
    AppendMenuW(menu, MF_STRING, kTrayFullscreen, fullscreenItem.c_str());
    AppendMenuW(menu, MF_STRING, kTrayRegion, regionItem.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_app.closeToTray ? MF_CHECKED : 0), kTrayToggleCloseToTray,
                L"关闭窗口时驻留托盘");
    AppendMenuW(menu, MF_STRING | (g_app.autoStart ? MF_CHECKED : 0), kTrayToggleAutoStart,
                L"开机自启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTraySettings, L"设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, L"退出 EasyQR");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);
    const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                        cursor.x, cursor.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    switch (command) {
        case kTrayOpen:
            ShowMainFromTray(hwnd);
            break;
        case kTrayFullscreen:
            StartScan(ScanMode::Fullscreen);
            break;
        case kTrayRegion:
            StartScan(ScanMode::Region);
            break;
        case kTraySettings:
            ShowMainFromTray(hwnd);
            SwitchView(MainView::Settings);
            break;
        case kTrayToggleCloseToTray:
            SetCloseToTrayEnabled(!g_app.closeToTray);
            break;
        case kTrayToggleAutoStart:
            ToggleAutoStart();
            break;
        case kTrayExit:
            QuitApplication(hwnd);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreated != 0 && message == taskbarCreated) {
        g_app.trayVisible = false;
        UpdateTrayPresence(hwnd);
        return 0;
    }

    switch (message) {
        case WM_HOTKEY:
            if (g_app.quitting || g_app.hotkeyCapture != AppState::HotkeyCapture::None) return 0;
            if (wParam == kHotkeyFullscreenId) {
                StartScan(ScanMode::Fullscreen);
            } else if (wParam == kHotkeyRegionId) {
                StartScan(ScanMode::Region);
            }
            return 0;
        case kTrayCallbackMessage:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowMainFromTray(hwnd);
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        case kShowMainMessage:
            ShowMainFromTray(hwnd);
            return 0;
        case kUpdateCheckDoneMessage:
            HandleUpdateCheckResult(hwnd, reinterpret_cast<UpdateCheckResult*>(lParam));
            return 0;
        case kUpdateReadyMessage:
            HandleUpdateReadyResult(hwnd, reinterpret_cast<UpdatePrepareResult*>(lParam));
            return 0;
        case kQuitForInstallerMessage:
            KillTimer(hwnd, kUpdateLaunchTimer);
            g_app.quitting = true;
            g_app.hotkeyCapture = AppState::HotkeyCapture::None;
            UnregisterConfiguredHotkeys();
            RemoveTrayIcon();
            DestroyWindow(hwnd);
            return 0;
        case WM_CREATE: {
            g_app.hwnd = hwnd;
            ApplyWindowIcons(hwnd);
            ApplyWindowTheme(hwnd, g_app.dark);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintMainWindow(hwnd);
            return 0;
        case WM_TIMER:
            if (wParam == kUpdateLaunchTimer) {
                KillTimer(hwnd, kUpdateLaunchTimer);
                if (g_app.updateState == UpdateState::Launching) {
                    ClearPendingUpdateVersion();
                    g_app.updateState = UpdateState::Error;
                    g_app.updateMessage = L"更新程序未响应，请重试";
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (wParam == kViewAnimationTimer && g_app.viewAnimationActive) {
                const double elapsed =
                    static_cast<double>(GetTickCount64() - g_app.viewAnimationStart);
                const double progress =
                    std::min(1.0, elapsed / static_cast<double>(kViewAnimationDurationMs));
                const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
                RECT current{};
                GetWindowRect(hwnd, &current);
                const int height =
                    g_app.viewAnimationStartHeight +
                    static_cast<int>(std::lround(
                        (g_app.viewAnimationTargetHeight - g_app.viewAnimationStartHeight) *
                        eased));
                SetWindowPos(hwnd, nullptr, 0, 0, current.right - current.left, height,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateWindow(hwnd);
                if (progress >= 1.0) {
                    g_app.viewAnimationActive = false;
                    KillTimer(hwnd, kViewAnimationTimer);
                }
                return 0;
            }
            break;
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                StopViewAnimation(hwnd);
                DiscardRenderTarget();
                return 0;
            }
            if (g_app.viewAnimationActive) {
                if (!g_app.compositionMode && g_app.rt) {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ResizeRenderTarget(
                        static_cast<UINT>(std::max<LONG>(1, client.right - client.left)),
                        static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top)));
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == SIZE_RESTORED) {
                RECT windowRect{};
                GetWindowRect(hwnd, &windowRect);
                const int targetHeight =
                    MulDiv(static_cast<int>(ClientHeightForView()), DpiOfWindow(hwnd), 96);
                if (std::abs((windowRect.bottom - windowRect.top) - targetHeight) > 1) {
                    ResizeWindowForView(hwnd);
                    return 0;
                }
            }
            if (g_app.rt) {
                RECT client{};
                GetClientRect(hwnd, &client);
                ResizeRenderTarget(
                    static_cast<UINT>(std::max<LONG>(1, client.right - client.left)),
                    static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top)));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DPICHANGED: {
            const UINT newDpi = HIWORD(wParam);
            g_app.dpi = newDpi ? newDpi : 96;
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            const int width = MulDiv(static_cast<int>(kClientWidth), g_app.dpi, 96);
            const int height = MulDiv(static_cast<int>(ClientHeightForView()), g_app.dpi, 96);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, width, height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_app.rt) {
                g_app.rt->SetDpi(static_cast<float>(g_app.dpi), static_cast<float>(g_app.dpi));
                if (g_app.compositionMode && g_app.d2dContext) {
                    g_app.d2dContext->SetDpi(static_cast<float>(g_app.dpi),
                                             static_cast<float>(g_app.dpi));
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ResizeRenderTarget(static_cast<UINT>(client.right),
                                       static_cast<UINT>(client.bottom));
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCACTIVATE:
            return TRUE;
        case WM_NCHITTEST: {
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &cursor);
            const float scale = static_cast<float>(g_app.dpi) / 96.0f;
            const float x = static_cast<float>(cursor.x) / scale;
            const float y = static_cast<float>(cursor.y) / scale;
            if (ControlAt(x, y) >= 0) return HTCLIENT;
            return HTCAPTION;
        }
        case WM_NCMOUSEMOVE:
            if (g_app.hoverControl != kControlNone) {
                g_app.hoverControl = kControlNone;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_ENTERSIZEMOVE:
            RefreshMainBackdropForDrag(hwnd);
            break;
        case WM_MOVE:
            if (GlassEnabled() && g_app.glassAvailable) {
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateWindow(hwnd);
            }
            return 0;
        case WM_NCPAINT:
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (message == WM_THEMECHANGED ||
                (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)) {
                RefreshTheme();
            }
            return 0;
        case WM_MOUSEMOVE: {
            const float scale = static_cast<float>(g_app.dpi) / 96.0f;
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
            const int control = ControlAt(x, y);
            if (control != g_app.hoverControl) {
                g_app.hoverControl = control;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            SetCursor(LoadCursorW(nullptr, UsesHandCursor(control) ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_app.hoverControl != kControlNone) {
                g_app.hoverControl = kControlNone;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            const float scale = static_cast<float>(g_app.dpi) / 96.0f;
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
            const int control = ControlAt(x, y);
            if (control >= 0) {
                g_app.keyboardFocus = false;
                g_app.pressedControl = control;
                g_app.focusControl = control;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const bool wasPressed = g_app.pressedControl >= 0;
            const int pressed = g_app.pressedControl;
            g_app.pressedControl = kControlNone;
            if (GetCapture() == hwnd) ReleaseCapture();

            const float scale = static_cast<float>(g_app.dpi) / 96.0f;
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
            const int control = ControlAt(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);

            if (wasPressed && control >= 0 && control == pressed) {
                ActivateControl(control);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (g_app.pressedControl != kControlNone) {
                g_app.pressedControl = kControlNone;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr,
                                      UsesHandCursor(g_app.hoverControl) ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_KEYDOWN: {
            if (g_app.hotkeyCapture != AppState::HotkeyCapture::None) {
                HandleHotkeyCaptureKey(wParam);
                return 0;
            }
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const std::vector<int> order = FocusOrder();

            if (wParam == VK_ESCAPE && g_app.view == MainView::Settings) {
                SwitchView(MainView::Home);
                return 0;
            }
            if (wParam == VK_TAB) {
                g_app.keyboardFocus = true;
                int current = -1;
                for (size_t i = 0; i < order.size(); ++i) {
                    if (order[i] == g_app.focusControl) current = static_cast<int>(i);
                }
                if (current < 0) {
                    g_app.focusControl = shift ? order.back() : order.front();
                } else {
                    const int count = static_cast<int>(order.size());
                    const int next = (current + (shift ? count - 1 : 1)) % count;
                    g_app.focusControl = order[static_cast<size_t>(next)];
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                g_app.keyboardFocus = true;
                if (g_app.view == MainView::Home) {
                    g_app.focusControl = wParam == VK_LEFT ? kFullScreen : kRegion;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (g_app.focusControl >= kGlassNone &&
                           g_app.focusControl <= kGlassDynamic) {
                    int selected = static_cast<int>(g_app.glassMode);
                    selected = (selected + (wParam == VK_RIGHT ? 1 : 2)) % 3;
                    SetGlassMode(static_cast<GlassMode>(selected));
                    g_app.focusControl = kGlassNone + selected;
                } else {
                    int selected = static_cast<int>(g_app.themeMode);
                    selected = (selected + (wParam == VK_RIGHT ? 1 : 2)) % 3;
                    SetThemeMode(static_cast<ThemeMode>(selected));
                    g_app.focusControl = kThemeSystem + selected;
                }
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                if (g_app.focusControl >= 0) ActivateControl(g_app.focusControl);
                return 0;
            }
            break;
        }
        case WM_SYSKEYDOWN:
            if (g_app.hotkeyCapture != AppState::HotkeyCapture::None) {
                HandleHotkeyCaptureKey(wParam);
                return 0;
            }
            break;
        case WM_SETFOCUS:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            if (g_app.hotkeyCapture != AppState::HotkeyCapture::None) {
                CancelHotkeyCapture(true);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (g_app.hotkeyCapture != AppState::HotkeyCapture::None) {
                    CancelHotkeyCapture(true);
                }
                if (g_app.pressedControl != kControlNone) {
                    g_app.pressedControl = kControlNone;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_CLOSE:
            if (!g_app.quitting && g_app.closeToTray) {
                HideToTray(hwnd);
            } else {
                QuitApplication(hwnd);
            }
            return 0;
        case WM_DESTROY:
            StopViewAnimation(hwnd);
            g_app.hotkeyCapture = AppState::HotkeyCapture::None;
            UnregisterConfiguredHotkeys();
            RemoveTrayIcon();
            DestroyOverlayForExit();
            JoinUpdateThread();
            g_app.hwnd = nullptr;
            DiscardRenderTarget();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

void SetStatus(std::wstring text, AppState::StatusKind kind) {
    g_app.status = std::move(text);
    g_app.statusKind = kind;
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

bool CaptureMainBackdropNow() {
    ScreenFrame frame;
    std::wstring error;
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
    if (!CaptureScreen(&frame, &error, false) ||
        !BuildGlassImage(frame, kMainGlassFactor, &pixels, &width, &height)) {
        return false;
    }

    g_app.glassPixels = std::move(pixels);
    g_app.glassWidth = width;
    g_app.glassHeight = height;
    g_app.glassScreenX = frame.x;
    g_app.glassScreenY = frame.y;
    g_app.glassScreenWidth = frame.width;
    g_app.glassScreenHeight = frame.height;
    g_app.glassAvailable = true;
    g_app.glassBitmap.Reset();
    if (g_app.rt) {
        CreateGlassBitmap();
    } else {
        UpdatePalette();
    }
    return true;
}

void RefreshMainBackdrop() {
    if (!g_app.hwnd) return;
    if (!GlassEnabled()) {
        g_app.glassBitmap.Reset();
        g_app.glassAvailable = false;
        DisableSystemBackdrop(g_app.hwnd);
        UpdatePalette();
        return;
    }

    if (g_app.glassMode == GlassMode::Dynamic) {
        if (g_app.dynamicBackdrop || EnableDynamicBackdrop(g_app.hwnd)) {
            InvalidateRect(g_app.hwnd, nullptr, FALSE);
            return;
        }
        g_app.glassBitmap.Reset();
        g_app.glassAvailable = false;
        if (IsWindowVisible(g_app.hwnd) && !IsIconic(g_app.hwnd) &&
            !g_app.glassPixels.empty()) {
            UpdatePalette();
            InvalidateRect(g_app.hwnd, nullptr, FALSE);
            return;
        }
    } else if (IsWindowVisible(g_app.hwnd) && !IsIconic(g_app.hwnd)) {
        return;
    }

    g_app.glassBitmap.Reset();
    if (!CaptureMainBackdropNow()) {
        g_app.glassPixels.clear();
        g_app.glassWidth = 0;
        g_app.glassHeight = 0;
        g_app.glassAvailable = false;
        if (g_app.compositionMode && g_app.rt) {
            if (g_app.glassMode == GlassMode::Dynamic) {
                EnableDynamicBackdrop(g_app.hwnd);
            } else {
                EnableStaticBackdropFallback(g_app.hwnd);
            }
        }
        UpdatePalette();
        InvalidateRect(g_app.hwnd, nullptr, FALSE);
        return;
    }
    InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void RefreshMainBackdropForDrag(HWND hwnd) {
    if (!hwnd || !GlassEnabled()) return;
    if (g_app.glassMode == GlassMode::Dynamic && g_app.compositionMode &&
        g_app.dynamicBackdrop) {
        return;
    }

    // Hide the window from one composition so the capture contains the current
    // desktop behind it rather than a snapshot of our own glass surface.
    BOOL cloak = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, kDwmaCloak, &cloak, sizeof(cloak)))) return;
    DwmFlush();
    const bool refreshed = CaptureMainBackdropNow();
    if (refreshed) {
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
    }
    cloak = FALSE;
    DwmSetWindowAttribute(hwnd, kDwmaCloak, &cloak, sizeof(cloak));
}

void ApplyWindowTheme(HWND hwnd, bool dark) {
    if (!hwnd) return;
    const BOOL useDark = dark ? TRUE : FALSE;
    SetDwmAttribute(hwnd, 20, &useDark, sizeof(useDark));  // DWMWA_USE_IMMERSIVE_DARK_MODE
    int corner = 2;                                        // DWMWCP_ROUND
    SetDwmAttribute(hwnd, 33, &corner, sizeof(corner));
}

void RefreshTheme() {
    g_app.systemDark = IsDarkThemePreferred();
    if (g_app.themeMode != ThemeMode::System) return;

    g_app.dark = g_app.systemDark;
    UpdatePalette();
    ApplyWindowTheme(g_app.hwnd, g_app.dark);
    if (g_app.compositionMode && g_app.rt && g_app.hwnd) {
        if (g_app.glassMode == GlassMode::Dynamic && !g_app.dynamicBackdrop) {
            EnableDynamicBackdrop(g_app.hwnd);
        } else if (GlassEnabled() && !g_app.glassAvailable) {
            EnableStaticBackdropFallback(g_app.hwnd);
        }
    }
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void SetThemeMode(ThemeMode mode) {
    g_app.themeMode = mode;
    if (mode == ThemeMode::System) g_app.systemDark = IsDarkThemePreferred();
    const bool dark = mode == ThemeMode::System ? g_app.systemDark : mode == ThemeMode::Dark;
    g_app.dark = dark;
    UpdatePalette();
    SaveThemeMode(mode);
    ApplyWindowTheme(g_app.hwnd, dark);
    if (g_app.compositionMode && g_app.rt && g_app.hwnd) {
        if (g_app.glassMode == GlassMode::Dynamic && !g_app.dynamicBackdrop) {
            EnableDynamicBackdrop(g_app.hwnd);
        } else if (GlassEnabled() && !g_app.glassAvailable) {
            EnableStaticBackdropFallback(g_app.hwnd);
        }
    }
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_app.instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    bool launchedHidden = false;
    bool updatedLaunch = false;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int i = 1; i < argumentCount; ++i) {
            if (_wcsicmp(arguments[i], L"--tray") == 0) launchedHidden = true;
            if (_wcsicmp(arguments[i], L"--updated") == 0) updatedLaunch = true;
        }
        LocalFree(arguments);
    }

    const std::wstring pendingVersion = LoadPendingUpdateVersion();
    const bool updateCompleted =
        !pendingVersion.empty() && IsValidVersion(pendingVersion) &&
        CompareVersions(EASYQR_VERSION_STRING, pendingVersion) >= 0;
    if (updateCompleted) updatedLaunch = true;

    std::vector<wchar_t> executablePath(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
                                                static_cast<DWORD>(executablePath.size()));
        if (length == 0) break;
        if (length < executablePath.size() - 1) {
            executablePath.resize(length);
            break;
        }
        executablePath.resize(executablePath.size() * 2);
    }
    struct MutexGuard {
        HANDLE handle = nullptr;
        ~MutexGuard() {
            if (handle) CloseHandle(handle);
        }
    } singleInstance;
    bool acquired = false;
    const int attempts = updatedLaunch ? 50 : 1;
    for (int attempt = 0; attempt < attempts && !acquired; ++attempt) {
        singleInstance.handle = CreateMutexW(nullptr, FALSE, L"Local\\EasyQR.SingleInstance");
        if (!singleInstance.handle) break;
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            acquired = true;
            break;
        }
        CloseHandle(singleInstance.handle);
        singleInstance.handle = nullptr;
        if (attempt + 1 < attempts) Sleep(100);
    }
    if (!acquired) {
        HWND existing = FindWindowW(EASYQR_MAIN_CLASS, nullptr);
        if (existing && !launchedHidden) PostMessageW(existing, kShowMainMessage, 0, 0);
        return 0;
    }

    if (!pendingVersion.empty()) {
        ClearPendingUpdateVersion();
        if (updateCompleted) {
            g_app.updateState = UpdateState::UpToDate;
            g_app.updateMessage = L"已更新到 v" + pendingVersion;
        } else if (IsValidVersion(pendingVersion)) {
            g_app.updateState = UpdateState::Error;
            g_app.updateMessage = L"上次更新未完成，请重新检查更新";
        }
    }

    if (!executablePath.empty()) {
        std::wstring currentExecutable(executablePath.begin(), executablePath.end());
        std::wstring oldExecutable = currentExecutable + L".old";
        DeleteFileW(oldExecutable.c_str());
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT,
                       currentExecutable.c_str(), nullptr);
    }
    CleanupUpdateCache();

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com)) {
        MessageBoxW(nullptr, L"无法初始化 COM 组件。", L"EasyQR", MB_ICONERROR | MB_OK);
        return 1;
    }

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_app.d2d.Put());
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Direct2D 初始化失败，无法启动 EasyQR。", L"EasyQR",
                    MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(g_app.dwrite.Put()));
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"DirectWrite 初始化失败，无法启动 EasyQR。", L"EasyQR",
                    MB_ICONERROR | MB_OK);
        g_app.d2d.Reset();
        CoUninitialize();
        return 1;
    }

    if (!CreateFonts(g_app.dwrite.Get(), &g_app.fonts)) {
        MessageBoxW(nullptr, L"字体初始化失败，无法启动 EasyQR。", L"EasyQR",
                    MB_ICONERROR | MB_OK);
        g_app.dwrite.Reset();
        g_app.d2d.Reset();
        CoUninitialize();
        return 1;
    }

    g_app.systemDark = IsDarkThemePreferred();
    g_app.themeMode = LoadThemeMode();
    g_app.closeToTray = LoadCloseToTray();
    g_app.glassMode = LoadGlassMode();
    g_app.autoStart = IsAutoStartEnabled();
    g_app.fullscreenHotkey = LoadHotkey(true);
    g_app.regionHotkey = LoadHotkey(false);
    g_app.launchedHidden = launchedHidden;
    g_app.dark = g_app.themeMode == ThemeMode::System ? g_app.systemDark
                                                      : g_app.themeMode == ThemeMode::Dark;
    g_app.compositionMode = ShouldUseComposition();
    UpdatePalette();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = EASYQR_MAIN_CLASS;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    const UINT classDpi = GetDpiForSystem();
    wc.hIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXICON, classDpi),
        GetSystemMetricsForDpi(SM_CYICON, classDpi), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, classDpi),
        GetSystemMetricsForDpi(SM_CYSMICON, classDpi), LR_SHARED));

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"窗口注册失败。", L"EasyQR", MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    const UINT systemDpi = GetDpiForSystem();
    const int windowWidth = MulDiv(static_cast<int>(kClientWidth), systemDpi, 96);
    const int windowHeight = MulDiv(static_cast<int>(ClientHeightForView()), systemDpi, 96);
    const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    DWORD exStyle = WS_EX_APPWINDOW;
    if (g_app.compositionMode) exStyle |= WS_EX_NOREDIRECTIONBITMAP;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int windowX = work.left + ((work.right - work.left) - windowWidth) / 2;
    const int windowY = work.top + ((work.bottom - work.top) - windowHeight) / 2;

    HWND hwnd = CreateWindowExW(exStyle, EASYQR_MAIN_CLASS, EASYQR_APP_NAME, style, windowX,
                               windowY, windowWidth, windowHeight, nullptr, nullptr, instance,
                               nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"主窗口创建失败。", L"EasyQR", MB_ICONERROR | MB_OK);
        g_app.fonts = FontSet{};
        g_app.dwrite.Reset();
        g_app.d2d.Reset();
        CoUninitialize();
        return 1;
    }

    g_app.dpi = DpiOfWindow(hwnd);
    CenterMainWindow(hwnd, windowWidth, windowHeight);
    RegisterCurrentHotkeys(true);

    if (g_app.launchedHidden) {
        if (!EnsureTrayIcon(hwnd)) {
            g_app.launchedHidden = false;
            g_app.settingsNotice = L"托盘图标创建失败，已显示主窗口";
            g_app.settingsNoticeError = true;
            RefreshMainBackdrop();
            ShowWindow(hwnd, SW_SHOW);
            ApplyWindowIcons(hwnd);
            UpdateWindow(hwnd);
        }
    } else {
        RefreshMainBackdrop();
        ShowWindow(hwnd, showCommand == SW_SHOWMAXIMIZED ? SW_SHOWNORMAL : showCommand);
        ApplyWindowIcons(hwnd);
        UpdateWindow(hwnd);
        if (g_app.closeToTray) EnsureTrayIcon(hwnd);
    }
    UpdateTrayPresence(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DiscardRenderTarget();
    g_app.fonts = FontSet{};
    g_app.dwrite.Reset();
    g_app.d2d.Reset();
    CoUninitialize();
    return static_cast<int>(message.wParam);
}

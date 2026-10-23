#include "app.h"
#include "capture.h"
#include "overlay.h"
#include "resource.h"
#include "update.h"
#include "version.h"

#include <shlobj.h>

#include <cstring>
#include <cwchar>

AppState g_app;

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
constexpr UINT_PTR kSliderTimer = 4;
constexpr UINT kViewAnimationDurationMs = 200;
constexpr UINT kUpdateLaunchTimeoutMs = 90000;
constexpr UINT kSliderAnimationDurationMs = 160;
constexpr DWORD kDwmaCloak = 13;  // DWMWA_CLOAK
constexpr DWORD kDwmaRedirectionBitmapAlpha = 39;  // DWMWA_REDIRECTIONBITMAP_ALPHA

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
    kStyleSolid = 19,
    kStyleAcrylic = 20,
    kStyleTranslucent = 21,
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
const D2D1_RECT_F kThemeSliderTrack = D2D1::RectF(104.0f, 82.0f, 376.0f, 116.0f);
const D2D1_RECT_F kGlassSliderTrack = D2D1::RectF(104.0f, 124.0f, 376.0f, 158.0f);
const D2D1_RECT_F kThemeLabelRect = D2D1::RectF(24.0f, 82.0f, 96.0f, 116.0f);
const D2D1_RECT_F kGlassLabelRect = D2D1::RectF(24.0f, 124.0f, 96.0f, 158.0f);

D2D1_RECT_F SegmentRect(const D2D1_RECT_F& track, int index, int count) {
    const float width = (track.right - track.left) / static_cast<float>(count);
    return D2D1::RectF(track.left + width * static_cast<float>(index), track.top,
                       track.left + width * static_cast<float>(index + 1), track.bottom);
}
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

void UpdatePalette();
void DiscardRenderTarget();
void EnsureRenderTarget(HWND hwnd);

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

// Reads CurrentBuildNumber so the probe can say which OS it actually ran on.
int ReadOsBuild() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return 0;
    }
    wchar_t buffer[32] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, &type,
                                           reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return 0;
    return _wtoi(buffer);
}

// Three states, because "the variant never asked for it" and "DWM refused" look
// identical to the person reading the line, and mean very different things.
enum ProbeCall {
    kCallNotTried = 0,
    kCallRefused,
    kCallAccepted,
};

// Experimental system-material path, selected with --probe=N on the command line.
// DWMWA_REDIRECTIONBITMAP_ALPHA (39) makes DWM honour the per-pixel alpha of an
// ordinary window surface (Windows 11 24H2 onwards); before that the surface is
// documented to be treated as fully opaque. With alpha honoured, the system
// backdrop drawn behind the surface becomes visible, so the material is sampled
// by DWM at composite time and cannot lag.
enum BackdropProbeVariant {
    kProbeOff = 0,
    kProbeAlphaOnly = 1,     // alpha only: the real desktop shows through our tint
    kProbeAcrylicFrame = 2,  // alpha + DWM acrylic + frame extended into the client
    kProbeAcrylicPlain = 3,  // alpha + DWM acrylic, frame left alone
    kProbeMicaFrame = 4,     // alpha + DWM mica + frame extended into the client
    kProbeAcrylicRaw = 5,    // acrylic + frame, no tint of ours at all
    kProbeTabbedFrame = 6,   // alpha + the tabbed-window material + frame
    kProbeAcrylicStandard = 7,  // acrylic + frame + framed window recipe
    kProbeMicaStandard = 8,     // mica + frame + framed window recipe
};

struct BackdropProbe {
    int variant = kProbeOff;
    bool requested = false;  // --probe was given, whichever variant is active now
    ProbeCall alpha = kCallNotTried;
    ProbeCall material = kCallNotTried;
    ProbeCall frame = kCallNotTried;
    bool standardFrame = false;  // keep WS_THICKFRAME and default WM_NCACTIVATE
    int build = 0;
    bool Active() const { return requested; }
    // The surface really carries alpha on this build and a variant is selected.
    bool Usable() const {
        return requested && alpha == kCallAccepted && variant != kProbeOff;
    }
};

BackdropProbe g_probe;

DWORD g_createdStyle = 0;

// Puts the window back into the style shape DWM wants for a framed top-level
// window when the system material is used, without giving up the borderless
// client area (WM_NCCALCSIZE still returns 0, so the layout is unchanged).
void SetFramedWindowStyle(HWND hwnd, bool framed) {
    if (!g_createdStyle) {
        g_createdStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    }
    DWORD style = g_createdStyle & ~static_cast<DWORD>(WS_THICKFRAME);
    if (framed) style |= WS_THICKFRAME;
    if (static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)) == style) return;
    SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(style));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void ApplyBackdropProbe(HWND hwnd) {
    if (!hwnd || !g_probe.Active()) return;
    g_probe.build = ReadOsBuild();
    g_probe.standardFrame =
        g_probe.variant == kProbeAcrylicStandard || g_probe.variant == kProbeMicaStandard;
    SetFramedWindowStyle(hwnd, g_probe.standardFrame);
    if (g_probe.variant == kProbeOff) return;
    const BOOL enabled = TRUE;
    g_probe.alpha = SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmaRedirectionBitmapAlpha, &enabled,
                                                   sizeof(enabled)))
                        ? kCallAccepted
                        : kCallRefused;
    if (g_probe.alpha != kCallAccepted) return;  // Pre-24H2: paint opaquely, but report it.

    int material = 0;
    bool wantsSheet = false;
    switch (g_probe.variant) {
        case kProbeAcrylicFrame:
        case kProbeAcrylicPlain:
        case kProbeAcrylicRaw:
        case kProbeAcrylicStandard:
            material = 3;  // DWMSBT_TRANSIENTWINDOW (acrylic)
            break;
        case kProbeMicaFrame:
        case kProbeMicaStandard:
            material = 2;  // DWMSBT_MAINWINDOW (mica)
            break;
        case kProbeTabbedFrame:
            material = 4;  // DWMSBT_TABBEDWINDOW
            break;
        default:
            break;
    }
    if (material) {
        g_probe.material = SUCCEEDED(DwmSetWindowAttribute(hwnd, 38, &material, sizeof(material)))
                               ? kCallAccepted
                               : kCallRefused;
    }
    switch (g_probe.variant) {
        case kProbeAcrylicFrame:
        case kProbeMicaFrame:
        case kProbeAcrylicRaw:
        case kProbeTabbedFrame:
        case kProbeAcrylicStandard:
        case kProbeMicaStandard:
            wantsSheet = true;
            break;
        default:
            break;
    }
    if (wantsSheet) {
        const MARGINS sheet{-1, -1, -1, -1};
        g_probe.frame = SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &sheet)) ? kCallAccepted
                                                                              : kCallRefused;
    }
}

// What DWM accepted for the selected style. Acrylic is the system material drawn
// behind the window (zero lag, sampled by the compositor); Translucent is the
// same per-pixel alpha without a material.
struct GlassStyleState {
    ProbeCall alpha = kCallNotTried;
    ProbeCall material = kCallNotTried;
    ProbeCall frame = kCallNotTried;
    bool framed = false;
};

GlassStyleState g_style;

bool AcrylicStyleVisible() {
    return g_app.glassMode == GlassMode::Acrylic && g_style.alpha == kCallAccepted &&
           g_style.material == kCallAccepted;
}

bool TranslucentStyleVisible() {
    return g_app.glassMode == GlassMode::Translucent && g_style.alpha == kCallAccepted;
}

// True when the surface carries real per-pixel alpha, so the page tint must be
// translucent instead of opaque.
bool TranslucentSurface() {
    return g_probe.Usable() || AcrylicStyleVisible() || TranslucentStyleVisible();
}

// Clears every backdrop attribute this window could carry, for the styles that do
// not use them and for the moment the window is hidden.
void ClearSystemBackdrop(HWND hwnd) {
    if (!hwnd) return;
    const BOOL alphaOff = FALSE;
    DwmSetWindowAttribute(hwnd, kDwmaRedirectionBitmapAlpha, &alphaOff, sizeof(alphaOff));
    const int none = 1;  // DWMSBT_NONE
    DwmSetWindowAttribute(hwnd, 38, &none, sizeof(none));
    const MARGINS zero{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &zero);
    int corner = 2;  // DWMWCP_ROUND
    SetDwmAttribute(hwnd, 33, &corner, sizeof(corner));
    SetFramedWindowStyle(hwnd, false);
}

// The material is composed behind the window frame, so a window that strips
// WS_THICKFRAME and answers WM_NCACTIVATE itself gets nothing at all. Both
// translucent styles start from per-pixel alpha (24H2+); acrylic additionally
// asks for the material and extends the frame into the client area.
void ApplyGlassStyle(HWND hwnd) {
    if (!hwnd || g_probe.Active()) return;
    const bool acrylic = g_app.glassMode == GlassMode::Acrylic;
    const bool translucent = g_app.glassMode == GlassMode::Translucent;
    g_style = GlassStyleState{};
    int corner = 2;  // DWMWCP_ROUND, in case the window was hidden square
    SetDwmAttribute(hwnd, 33, &corner, sizeof(corner));
    if (!acrylic && !translucent) {
        ClearSystemBackdrop(hwnd);
        return;
    }

    const BOOL alphaOn = TRUE;
    g_style.alpha = SUCCEEDED(
                        DwmSetWindowAttribute(hwnd, kDwmaRedirectionBitmapAlpha, &alphaOn,
                                              sizeof(alphaOn)))
                        ? kCallAccepted
                        : kCallRefused;
    if (g_style.alpha != kCallAccepted) {
        // Pre-24H2: nothing can show through, so the solid page is the whole style.
        ClearSystemBackdrop(hwnd);
        return;
    }
    if (!acrylic) {
        const int none = 1;
        DwmSetWindowAttribute(hwnd, 38, &none, sizeof(none));
        const MARGINS zero{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(hwnd, &zero);
        g_style.framed = false;
        return;
    }
    int material = 3;  // DWMSBT_TRANSIENTWINDOW (acrylic)
    g_style.material = SUCCEEDED(DwmSetWindowAttribute(hwnd, 38, &material, sizeof(material)))
                           ? kCallAccepted
                           : kCallRefused;
    const MARGINS sheet{-1, -1, -1, -1};
    g_style.frame =
        SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &sheet)) ? kCallAccepted : kCallRefused;
    g_style.framed = true;
}

// The material (or the desktop) is behind the surface, so the page colour is
// painted as a translucent veil instead of an opaque fill.
float StyleVeilAlpha() {
    if (AcrylicStyleVisible()) return g_app.dark ? 0.42f : 0.58f;
    return g_app.dark ? 0.74f : 0.80f;
}

float ProbeTintAlpha() {
    if (g_probe.material == kCallAccepted) return g_app.dark ? 0.42f : 0.58f;
    return g_app.dark ? 0.74f : 0.80f;
}

// Switches variants at runtime so one real-machine session can compare them all,
// with F12 going back to the shipped path. Only reachable when the app was
// started with --probe.
void SwitchProbeVariant(HWND hwnd, int variant) {
    if (!hwnd || variant < kProbeOff || variant > kProbeMicaStandard) return;
    const BOOL off = FALSE;
    DwmSetWindowAttribute(hwnd, kDwmaRedirectionBitmapAlpha, &off, sizeof(off));
    const int none = 1;
    DwmSetWindowAttribute(hwnd, 38, &none, sizeof(none));
    const MARGINS zero{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &zero);
    SetFramedWindowStyle(hwnd, false);  // the probe still tests framed vs unframed

    const bool requested = g_probe.requested;
    g_probe = BackdropProbe{};
    g_probe.requested = requested;
    g_probe.variant = variant;
    ApplyBackdropProbe(hwnd);
    DiscardRenderTarget();
    EnsureRenderTarget(hwnd);
    RefreshMainBackdrop();
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Paints the translucent page colour over whatever DWM put behind the surface.
void PaintTranslucentVeil(ID2D1RenderTarget* rt, HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = 96.0f / static_cast<float>(g_app.dpi);
    const D2D1_RECT_F area =
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(client.right) * scale,
                    static_cast<float>(client.bottom) * scale);
    const float alpha = g_probe.Active() ? ProbeTintAlpha() : StyleVeilAlpha();
    SetBrushColor(g_app.brush.Get(), WithAlpha(g_app.palette.page, alpha));
    rt->FillRectangle(area, g_app.brush.Get());
}

bool InitializeHwndRenderer(HWND hwnd, UINT width, UINT height) {
    // The probe path needs real per-pixel alpha out of the surface; the shipped
    // path stays opaque because that is all an older DWM honours.
    const D2D1_ALPHA_MODE alphaMode = TranslucentSurface() ? D2D1_ALPHA_MODE_PREMULTIPLIED
                                                           : D2D1_ALPHA_MODE_IGNORE;
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, alphaMode), 96.0f, 96.0f);

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
    if (!CreateRenderBrush()) {
        g_app.hwndTarget = nullptr;
        g_app.rt.Reset();
        return false;
    }
    UpdatePalette();
    return true;
}

void EnsureRenderTarget(HWND hwnd) {
    if (g_app.rt || !g_app.d2d) return;

    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
    const UINT height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

    InitializeHwndRenderer(hwnd, width, height);
}

void ResizeRenderTarget(UINT width, UINT height) {
    if (g_app.hwndTarget) {
        g_app.hwndTarget->Resize(D2D1::SizeU(width, height));
    }
}

void DiscardRenderTarget() {
    g_app.brush.Reset();
    g_app.hwndTarget = nullptr;
    g_app.rt.Reset();
}

void UpdatePalette() {
    g_app.palette = MakePalette(g_app.dark);
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
        if (PointInRect(SegmentRect(kThemeSliderTrack, i, 3), x, y)) return kThemeSystem + i;
    }
    for (int i = 0; i < 3; ++i) {
        if (PointInRect(SegmentRect(kGlassSliderTrack, i, 3), x, y)) return kStyleSolid + i;
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
    return {kBack,          kThemeSystem,       kThemeLight,       kThemeDark,
            kStyleSolid,    kStyleAcrylic,      kStyleTranslucent, kCloseToTrayControl,
            kAutoStartControl, kHotkeyFullscreenControl, kHotkeyRegionControl,
            kUpdateButton};
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
    // Same geometry as assets/easyqr.svg so the title bar mark, window icon,
    // tray icon and shortcuts all show the identical design.
    const float size = RectWidth(rect);
    const float inset = size * (64.0f / 1024.0f);
    const D2D1_RECT_F badge =
        D2D1::RectF(rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset);
    const float radius = size * (224.0f / 1024.0f);
    SetBrushColor(g_app.brush.Get(), Rgb(0x0078D4));
    rt->FillRoundedRectangle(D2D1::RoundedRect(badge, radius, radius), g_app.brush.Get());

    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(factory.Put());
    if (!factory) return;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f);
    ComPtr<ID2D1StrokeStyle> strokeStyle;
    if (FAILED(factory->CreateStrokeStyle(props, nullptr, 0, strokeStyle.Put())) || !strokeStyle) {
        return;
    }

    const float unit = size / 1024.0f;
    auto point = [&](float x, float y) {
        return D2D1::Point2F(rect.left + x * unit, rect.top + y * unit);
    };
    SetBrushColor(g_app.brush.Get(), D2D1::ColorF(D2D1::ColorF::White));
    const float stroke = std::max(1.0f, size * (70.0f / 1024.0f));
    auto segment = [&](float ax, float ay, float bx, float by) {
        rt->DrawLine(point(ax, ay), point(bx, by), g_app.brush.Get(), stroke, strokeStyle.Get());
    };
    segment(260, 480, 260, 260);
    segment(260, 260, 440, 260);
    segment(584, 260, 764, 260);
    segment(764, 260, 764, 480);
    segment(764, 544, 764, 764);
    segment(764, 764, 584, 764);
    segment(440, 764, 260, 764);
    segment(260, 764, 260, 544);
    segment(370, 512, 654, 512);
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

float SliderPosition(const AppState::SliderAnimation& anim, float selected) {
    if (!anim.active) return selected;
    const double elapsed = static_cast<double>(GetTickCount64() - anim.start);
    const double progress =
        std::min(1.0, elapsed / static_cast<double>(kSliderAnimationDurationMs));
    const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
    return anim.from + (anim.to - anim.from) * static_cast<float>(eased);
}

void BeginSliderAnimation(AppState::SliderAnimation& anim, float target) {
    const float current = anim.active ? SliderPosition(anim, anim.to) : anim.to;
    if (std::fabs(current - target) < 0.001f) {
        anim.active = false;
        anim.to = target;
        return;
    }
    anim.from = current;
    anim.to = target;
    anim.start = GetTickCount64();
    anim.active = true;
    if (g_app.hwnd) SetTimer(g_app.hwnd, kSliderTimer, 10, nullptr);
}

const wchar_t* GlassLabel(GlassMode mode) {
    switch (mode) {
        case GlassMode::Acrylic:
            return L"毛玻璃";
        case GlassMode::Translucent:
            return L"半透明";
        default:
            return L"纯色";
    }
}

void DrawSegmentedSlider(ID2D1RenderTarget* rt, const D2D1_RECT_F& track,
                         const wchar_t* const* labels, int count, int firstControl, int selected,
                         float thumbPosition) {
    const float radius = RectHeight(track) * 0.5f;
    SetBrushColor(g_app.brush.Get(), g_app.palette.card);
    rt->FillRoundedRectangle(D2D1::RoundedRect(track, radius, radius), g_app.brush.Get());
    SetBrushColor(g_app.brush.Get(), g_app.palette.border);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(track, radius, radius), g_app.brush.Get(), 1.0f);

    const float segmentWidth = (track.right - track.left) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        const int control = firstControl + i;
        const bool hovered = g_app.hoverControl == control;
        const bool pressed = g_app.pressedControl == control;
        if ((hovered || pressed) && i != selected) {
            const D2D1_RECT_F hoverRect = SegmentRect(track, i, count);
            const D2D1_RECT_F inset =
                D2D1::RectF(hoverRect.left + 3.0f, hoverRect.top + 3.0f, hoverRect.right - 3.0f,
                            hoverRect.bottom - 3.0f);
            const float hoverRadius = RectHeight(inset) * 0.5f;
            SetBrushColor(g_app.brush.Get(), pressed ? g_app.palette.cardPressed
                                                     : g_app.palette.cardHover);
            rt->FillRoundedRectangle(D2D1::RoundedRect(inset, hoverRadius, hoverRadius),
                                     g_app.brush.Get());
        }
    }

    const D2D1_RECT_F thumb =
        D2D1::RectF(track.left + segmentWidth * thumbPosition + 3.0f, track.top + 3.0f,
                    track.left + segmentWidth * (thumbPosition + 1.0f) - 3.0f, track.bottom - 3.0f);
    const float thumbRadius = RectHeight(thumb) * 0.5f;
    SetBrushColor(g_app.brush.Get(), g_app.palette.accentSoftHover);
    rt->FillRoundedRectangle(D2D1::RoundedRect(thumb, thumbRadius, thumbRadius),
                             g_app.brush.Get());
    SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(thumb, thumbRadius, thumbRadius),
                             g_app.brush.Get(), 1.2f);

    for (int i = 0; i < count; ++i) {
        const int control = firstControl + i;
        const bool isSelected = i == selected;
        const bool hovered = g_app.hoverControl == control;
        const bool focused = IsFocused(control);
        const D2D1_RECT_F labelRect = SegmentRect(track, i, count);
        SetBrushColor(g_app.brush.Get(), isSelected || hovered || focused
                                             ? g_app.palette.accent
                                             : g_app.palette.secondary);
        DrawTextEx(rt, g_app.dwrite.Get(), labels[i], g_app.fonts.chip.Get(), labelRect,
                   g_app.brush.Get(), TextAlign::Center, true, true);
        if (focused) {
            const D2D1_RECT_F focusRect =
                D2D1::RectF(labelRect.left + 4.0f, labelRect.top + 4.0f, labelRect.right - 4.0f,
                            labelRect.bottom - 4.0f);
            const float focusRadius = RectHeight(focusRect) * 0.5f;
            SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(focusRect, focusRadius, focusRadius),
                                     g_app.brush.Get(), 1.2f);
        }
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
    DrawTextEx(rt, g_app.dwrite.Get(), L"外观", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 64.0f, 200.0f, 80.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), L"主题", g_app.fonts.cardBody.Get(), kThemeLabelRect,
               g_app.brush.Get(), TextAlign::Leading, true, true);
    DrawTextEx(rt, g_app.dwrite.Get(), L"样式", g_app.fonts.cardBody.Get(), kGlassLabelRect,
               g_app.brush.Get(), TextAlign::Leading, true, true);

    const wchar_t* const themeLabels[3] = {ThemeLabel(ThemeMode::System),
                                           ThemeLabel(ThemeMode::Light),
                                           ThemeLabel(ThemeMode::Dark)};
    const wchar_t* const glassLabels[3] = {GlassLabel(GlassMode::Solid),
                                           GlassLabel(GlassMode::Acrylic),
                                           GlassLabel(GlassMode::Translucent)};
    DrawSegmentedSlider(rt, kThemeSliderTrack, themeLabels, 3, kThemeSystem,
                        static_cast<int>(g_app.themeMode),
                        SliderPosition(g_app.themeSlider, static_cast<float>(g_app.themeMode)));
    DrawSegmentedSlider(rt, kGlassSliderTrack, glassLabels, 3, kStyleSolid,
                        static_cast<int>(g_app.glassMode),
                        SliderPosition(g_app.glassSlider, static_cast<float>(g_app.glassMode)));

    std::wstring glassNote = L"纯色：不透明，不请求系统材质";
    D2D1_COLOR_F glassNoteColor = g_app.palette.tertiary;
    if (g_app.glassMode == GlassMode::Acrylic) {
        glassNote = AcrylicStyleVisible() ? L"毛玻璃由系统绘制，拖动与视频都不会有延迟"
                                          : L"毛玻璃需要 Windows 11 24H2 及以上，当前按纯色显示";
        if (!AcrylicStyleVisible()) glassNoteColor = g_app.palette.secondary;
    } else if (g_app.glassMode == GlassMode::Translucent) {
        glassNote = TranslucentStyleVisible() ? L"半透明：桌面直接透出，不做模糊"
                                              : L"半透明需要 Windows 11 24H2 及以上，当前按纯色显示";
        if (!TranslucentStyleVisible()) glassNoteColor = g_app.palette.secondary;
    }
    if (g_probe.Active()) {
        // Built by concatenation: the CRT's swprintf reads %s as a narrow string.
        const auto flag = [](ProbeCall call) {
            if (call == kCallAccepted) return L"OK";
            return call == kCallRefused ? L"拒绝" : L"未试";
        };
        std::wstring note = L"探针 " + std::to_wstring(g_probe.variant) + L" · 构建 " +
                            std::to_wstring(g_probe.build);
        if (g_probe.variant == kProbeOff) {
            note += L"（F1-F8 重新开启）";
        } else {
            note += L" · alpha=" + std::wstring(flag(g_probe.alpha)) +
                    L" · 材质=" + std::wstring(flag(g_probe.material)) +
                    L" · 边框=" + std::wstring(flag(g_probe.frame)) +
                    (g_probe.standardFrame ? L" · 带框" : L"");
        }
        glassNote = std::move(note);
        glassNoteColor = g_app.palette.accent;
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
    if (TranslucentSurface()) {
        rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        if (!g_probe.Active() || g_probe.variant != kProbeAcrylicRaw) {
            PaintTranslucentVeil(rt, hwnd);
        }
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
    if (control >= kStyleSolid && control <= kStyleTranslucent) {
        SetGlassMode(static_cast<GlassMode>(control - kStyleSolid));
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
    // Show first, then ask DWM for the material: a backdrop requested while the
    // window is not on screen is never composed, which lost the glass every time
    // the window came back from the tray.
    ShowWindow(hwnd, SW_RESTORE);
    RefreshMainBackdrop();
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
    PrepareWindowForHide(hwnd);
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
    if (hwnd) {
        PrepareWindowForHide(hwnd);
        DestroyWindow(hwnd);
    }
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

void SetGlassMode(GlassMode mode) {
    if (g_app.glassMode == mode) return;
    HWND hwnd = g_app.hwnd;
    if (hwnd && g_app.viewAnimationActive) CompleteViewAnimation(hwnd);
    BeginSliderAnimation(g_app.glassSlider, static_cast<float>(mode));
    g_app.glassMode = mode;
    SaveGlassMode(mode);
    RefreshMainBackdrop();
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
            if (wParam == kSliderTimer) {
                bool animating = false;
                const ULONGLONG now = GetTickCount64();
                for (AppState::SliderAnimation* anim :
                     {&g_app.themeSlider, &g_app.glassSlider}) {
                    if (!anim->active) continue;
                    if (now - anim->start >= kSliderAnimationDurationMs) {
                        anim->active = false;
                    } else {
                        animating = true;
                    }
                }
                if (!animating) KillTimer(hwnd, kSliderTimer);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
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
                // The HWND render target must match the client size: a larger
                // surface gets stretched into the window and squashes the layout.
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
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCACTIVATE:
            // Answering this ourselves keeps DefWindowProc from painting the
            // classic non-client frame; with the frame extended into the client
            // area that paint covered the whole window for a frame or two.
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
            if (g_probe.Active() && wParam >= VK_F1 && wParam <= VK_F8) {
                SwitchProbeVariant(hwnd, static_cast<int>(wParam - VK_F1) + kProbeAlphaOnly);
                return 0;
            }
            if (g_probe.Active() && wParam == VK_F12) {
                SwitchProbeVariant(hwnd, kProbeOff);
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
                } else if (g_app.focusControl >= kStyleSolid &&
                           g_app.focusControl <= kStyleTranslucent) {
                    int selected = static_cast<int>(g_app.glassMode);
                    selected = (selected + (wParam == VK_RIGHT ? 1 : 2)) % 3;
                    SetGlassMode(static_cast<GlassMode>(selected));
                    g_app.focusControl = kStyleSolid + selected;
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

void PrepareWindowForHide(HWND hwnd) {
    if (!hwnd) return;
    // DWM keeps the material and the rounded frame of an extended-frame window
    // composed after it is hidden, which left a dark card (or a 1px ring) on the
    // desktop. Drop the backdrop state while the window is still visible, so it
    // hides as an ordinary opaque window.
    const BOOL alphaOff = FALSE;
    DwmSetWindowAttribute(hwnd, kDwmaRedirectionBitmapAlpha, &alphaOff, sizeof(alphaOff));
    const int none = 1;
    DwmSetWindowAttribute(hwnd, 38, &none, sizeof(none));
    const MARGINS zero{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &zero);
    int square = 1;  // DWMWCP_DONOTROUND while hidden
    SetDwmAttribute(hwnd, 33, &square, sizeof(square));
}
// Re-applies the selected style to the window. The render target has to be
// rebuilt whenever the surface alpha mode changes, which is why this is not just
// a DWM call.
void RefreshMainBackdrop() {
    HWND hwnd = g_app.hwnd;
    if (!hwnd) return;
    ApplyGlassStyle(hwnd);
    if (g_app.rt) {
        DiscardRenderTarget();
        EnsureRenderTarget(hwnd);
    }
    UpdatePalette();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ApplyWindowTheme(HWND hwnd, bool dark) {
    if (!hwnd) return;
    const BOOL useDark = dark ? TRUE : FALSE;
    SetDwmAttribute(hwnd, 20, &useDark, sizeof(useDark));  // DWMWA_USE_IMMERSIVE_DARK_MODE
    int corner = 2;                                        // DWMWCP_ROUND
    SetDwmAttribute(hwnd, 33, &corner, sizeof(corner));
    // The 1px rounded ring is drawn by DWM, not by us. It is not covered by capture
    // exclusion and DWM keeps it composed after the window hides, which is the
    // frame left burnt into the desktop. Suppress it and draw our edge ourselves.
    const COLORREF none = 0xFFFFFFFE;  // DWMWA_COLOR_NONE
    SetDwmAttribute(hwnd, 34, &none, sizeof(none));  // DWMWA_BORDER_COLOR
}

void RefreshTheme() {
    g_app.systemDark = IsDarkThemePreferred();
    if (g_app.themeMode != ThemeMode::System) return;

    g_app.dark = g_app.systemDark;
    UpdatePalette();
    ApplyWindowTheme(g_app.hwnd, g_app.dark);
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void SetThemeMode(ThemeMode mode) {
    if (g_app.themeMode != mode) BeginSliderAnimation(g_app.themeSlider, static_cast<float>(mode));
    g_app.themeMode = mode;
    if (mode == ThemeMode::System) g_app.systemDark = IsDarkThemePreferred();
    const bool dark = mode == ThemeMode::System ? g_app.systemDark : mode == ThemeMode::Dark;
    g_app.dark = dark;
    UpdatePalette();
    SaveThemeMode(mode);
    ApplyWindowTheme(g_app.hwnd, dark);
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
            if (_wcsnicmp(arguments[i], L"--probe=", 8) == 0) {
                const int variant = _wtoi(arguments[i] + 8);
                if (variant >= kProbeOff && variant <= kProbeMicaStandard) {
                    g_probe.requested = true;
                    g_probe.variant = variant;
                }
            }
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
    g_app.themeSlider.to = static_cast<float>(g_app.themeMode);
    g_app.glassSlider.to = static_cast<float>(g_app.glassMode);
    g_app.autoStart = IsAutoStartEnabled();
    g_app.fullscreenHotkey = LoadHotkey(true);
    g_app.regionHotkey = LoadHotkey(false);
    g_app.launchedHidden = launchedHidden;
    g_app.dark = g_app.themeMode == ThemeMode::System ? g_app.systemDark
                                                      : g_app.themeMode == ThemeMode::Dark;
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
    // WS_THICKFRAME stays for the window's whole life: DWM only composes a system
    // material behind a window it treats as framed. The caption stays as well --
    // without it the window was unusable on real hardware (fully transparent and
    // unresponsive), so the activation flash is handled in WM_NCACTIVATE instead.
    const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
    // The window must keep a redirection bitmap: WS_EX_NOREDIRECTIONBITMAP would
    // leave DWM no surface to draw the system Acrylic/Mica backdrop into, so the
    // dynamic glass mode would render nothing.
    const DWORD exStyle = WS_EX_APPWINDOW;

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
    ApplyBackdropProbe(hwnd);
    ApplyGlassStyle(hwnd);
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
        ShowWindow(hwnd, showCommand == SW_SHOWMAXIMIZED ? SW_SHOWNORMAL : showCommand);
        RefreshMainBackdrop();
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

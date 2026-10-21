#include "app.h"
#include "overlay.h"
#include "resource.h"
#include "version.h"

#include <cwchar>

AppState g_app;

namespace {

constexpr float kClientWidth = 400.0f;
constexpr float kHomeClientHeight = 240.0f;
constexpr float kSettingsClientHeight = 500.0f;

constexpr UINT kTrayCallbackMessage = WM_APP + 20;
constexpr UINT kShowMainMessage = WM_APP + 21;
constexpr UINT kQuitForInstallerMessage = WM_APP + 22;
constexpr UINT kHotkeyFullscreenId = 1;
constexpr UINT kHotkeyRegionId = 2;

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
    kBack = 10,
    kThemeSystem = 11,
    kThemeLight = 12,
    kThemeDark = 13,
    kCloseToTrayControl = 14,
    kAutoStartControl = 15,
    kHotkeyFullscreenControl = 16,
    kHotkeyRegionControl = 17,
};

const D2D1_RECT_F kTileRects[2] = {
    D2D1::RectF(24.0f, 48.0f, 192.0f, 208.0f),
    D2D1::RectF(208.0f, 48.0f, 376.0f, 208.0f),
};
const D2D1_RECT_F kSettingsRect = D2D1::RectF(344.0f, 12.0f, 376.0f, 44.0f);
const D2D1_RECT_F kBackRect = D2D1::RectF(16.0f, 10.0f, 48.0f, 42.0f);
const D2D1_RECT_F kThemeRects[3] = {
    D2D1::RectF(24.0f, 88.0f, 136.0f, 122.0f),
    D2D1::RectF(144.0f, 88.0f, 256.0f, 122.0f),
    D2D1::RectF(264.0f, 88.0f, 376.0f, 122.0f),
};
const D2D1_RECT_F kCloseToTrayRow = D2D1::RectF(24.0f, 170.0f, 376.0f, 204.0f);
const D2D1_RECT_F kCloseToTraySwitch = D2D1::RectF(328.0f, 174.0f, 376.0f, 200.0f);
const D2D1_RECT_F kAutoStartRow = D2D1::RectF(24.0f, 210.0f, 376.0f, 244.0f);
const D2D1_RECT_F kAutoStartSwitch = D2D1::RectF(328.0f, 214.0f, 376.0f, 240.0f);
const D2D1_RECT_F kHotkeyFullscreenButton = D2D1::RectF(196.0f, 290.0f, 376.0f, 324.0f);
const D2D1_RECT_F kHotkeyRegionButton = D2D1::RectF(196.0f, 328.0f, 376.0f, 362.0f);

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

void EnsureRenderTarget(HWND hwnd) {
    if (g_app.rt || !g_app.d2d) return;

    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
    const UINT height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);

    ComPtr<ID2D1HwndRenderTarget> target;
    HRESULT hr = g_app.d2d->CreateHwndRenderTarget(
        properties, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)),
        target.Put());
    if (FAILED(hr) || !target) return;

    target->SetDpi(static_cast<float>(g_app.dpi), static_cast<float>(g_app.dpi));
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    ComPtr<ID2D1SolidColorBrush> brush;
    hr = target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush.Put());
    if (FAILED(hr) || !brush) return;

    g_app.rt = std::move(target);
    g_app.brush = std::move(brush);
}

void DiscardRenderTarget() {
    g_app.brush.Reset();
    g_app.rt.Reset();
}

bool IsFocused(int control) {
    return g_app.keyboardFocus && g_app.focusControl == control && GetFocus() == g_app.hwnd;
}

void CancelHotkeyCapture(bool restoreHotkeys);
void BeginHotkeyCapture(bool fullscreen);
void SetCloseToTrayEnabled(bool enabled);
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

void ResizeWindowForView(HWND hwnd) {
    if (!hwnd) return;
    const UINT dpi = DpiOfWindow(hwnd);
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    RECT windowRect{0, 0, MulDiv(static_cast<int>(kClientWidth), dpi, 96),
                    MulDiv(static_cast<int>(ClientHeightForView()), dpi, 96)};
    AdjustWindowRectExForDpi(&windowRect, style, FALSE, exStyle, dpi);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;

    RECT work{};
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
        work = info.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    const int x = work.left + std::max(0, (static_cast<int>(work.right - work.left) - width) / 2);
    const int y = work.top + std::max(0, (static_cast<int>(work.bottom - work.top) - height) / 2);
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

int ControlAt(float x, float y) {
    if (g_app.view == MainView::Home) {
        for (int i = 0; i < 2; ++i) {
            if (PointInRect(kTileRects[i], x, y)) return i;
        }
        if (PointInRect(kSettingsRect, x, y)) return kSettings;
        return kControlNone;
    }

    if (PointInRect(kBackRect, x, y)) return kBack;
    for (int i = 0; i < 3; ++i) {
        if (PointInRect(kThemeRects[i], x, y)) return kThemeSystem + i;
    }
    if (PointInRect(kCloseToTrayRow, x, y)) return kCloseToTrayControl;
    if (PointInRect(kAutoStartRow, x, y)) return kAutoStartControl;
    if (PointInRect(kHotkeyFullscreenButton, x, y)) return kHotkeyFullscreenControl;
    if (PointInRect(kHotkeyRegionButton, x, y)) return kHotkeyRegionControl;
    return kControlNone;
}

std::vector<int> FocusOrder() {
    if (g_app.view == MainView::Home) return {kFullScreen, kRegion, kSettings};
    return {kBack,       kThemeSystem, kThemeLight,           kThemeDark,
            kCloseToTrayControl, kAutoStartControl, kHotkeyFullscreenControl, kHotkeyRegionControl};
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
        ResizeWindowForView(g_app.hwnd);
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

void DrawIconButton(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, IconId icon, int control) {
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

void DrawTile(ID2D1HwndRenderTarget* rt, int index) {
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

void DrawStatus(ID2D1HwndRenderTarget* rt) {
    if (g_app.statusKind == AppState::StatusKind::None || g_app.status.empty()) return;

    const D2D1_COLOR_F color = g_app.statusKind == AppState::StatusKind::Error
                                   ? g_app.palette.danger
                                   : g_app.palette.secondary;
    SetBrushColor(g_app.brush.Get(), color);
    DrawTextEx(rt, g_app.dwrite.Get(), g_app.status, g_app.fonts.footer.Get(),
               D2D1::RectF(24.0f, 212.0f, 376.0f, 234.0f), g_app.brush.Get(),
               TextAlign::Center, true, true);
}

void DrawHome(ID2D1HwndRenderTarget* rt) {
    for (int i = 0; i < 2; ++i) DrawTile(rt, i);
    DrawIconButton(rt, kSettingsRect, IconId::Settings, kSettings);
    DrawStatus(rt);
}

void DrawThemeButton(ID2D1HwndRenderTarget* rt, int index) {
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

void DrawSwitch(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool on, bool hovered,
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

void DrawToggleRow(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& row, const D2D1_RECT_F& sw,
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

void DrawHotkeyButton(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, const std::wstring& label,
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

void DrawSettings(ID2D1HwndRenderTarget* rt) {
    DrawIconButton(rt, kBackRect, IconId::ArrowLeft, kBack);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), L"设置", g_app.fonts.heading.Get(),
               D2D1::RectF(56.0f, 10.0f, 300.0f, 42.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 54.0f), D2D1::Point2F(kClientWidth - 24.0f, 54.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"外观", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 64.0f, 200.0f, 84.0f), g_app.brush.Get(), TextAlign::Leading,
               true);
    for (int i = 0; i < 3; ++i) DrawThemeButton(rt, i);

    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 136.0f), D2D1::Point2F(kClientWidth - 24.0f, 136.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"常规", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 146.0f, 200.0f, 166.0f), g_app.brush.Get(), TextAlign::Leading,
               true);
    DrawToggleRow(rt, kCloseToTrayRow, kCloseToTraySwitch, L"关闭窗口时最小化到托盘",
                  kCloseToTrayControl, g_app.closeToTray);
    DrawToggleRow(rt, kAutoStartRow, kAutoStartSwitch, L"开机自启动（仅托盘）",
                  kAutoStartControl, g_app.autoStart);

    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 256.0f), D2D1::Point2F(kClientWidth - 24.0f, 256.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"快捷键", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 266.0f, 200.0f, 286.0f), g_app.brush.Get(), TextAlign::Leading,
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
               D2D1::RectF(24.0f, 366.0f, 376.0f, 388.0f), g_app.brush.Get(),
               TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(24.0f, 396.0f), D2D1::Point2F(kClientWidth - 24.0f, 396.0f),
                 g_app.brush.Get(), 1.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"关于", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 406.0f, 200.0f, 426.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), L"EasyQR " EASYQR_VERSION_STRING, g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 430.0f, 260.0f, 450.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.tertiary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"MIT License", g_app.fonts.footer.Get(),
               D2D1::RectF(264.0f, 430.0f, 376.0f, 450.0f), g_app.brush.Get(),
               TextAlign::Trailing, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"图像与扫描结果仅在本机内存中处理，不作保存。",
               g_app.fonts.footer.Get(), D2D1::RectF(24.0f, 454.0f, 376.0f, 472.0f),
               g_app.brush.Get(), TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.tertiary);
    DrawTextEx(rt, g_app.dwrite.Get(),
               L"解码：ZXing-C++ + quirc  ·  设置保存在本机，卸载时删除",
               g_app.fonts.chipSmall.Get(), D2D1::RectF(24.0f, 474.0f, 376.0f, 492.0f),
               g_app.brush.Get(), TextAlign::Leading, true, true);
}

void PaintMainWindow(HWND hwnd) {
    EnsureRenderTarget(hwnd);
    if (!g_app.rt) return;

    PAINTSTRUCT ps{};
    BeginPaint(hwnd, &ps);
    ID2D1HwndRenderTarget* rt = g_app.rt.Get();
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->SetDpi(static_cast<float>(g_app.dpi), static_cast<float>(g_app.dpi));
    rt->Clear(g_app.palette.page);

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
    if (control == kBack) {
        SwitchView(MainView::Home);
        return;
    }
    if (control >= kThemeSystem && control <= kThemeDark) {
        SetThemeMode(ThemeForControl(control));
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
    g_trayData.hIcon = static_cast<HICON>(LoadImageW(
        g_app.instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!g_trayData.hIcon) {
        g_trayData.hIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP));
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

void ShowMainFromTray(HWND hwnd) {
    if (!hwnd) return;
    g_app.launchedHidden = false;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateTrayPresence(hwnd);
}

void HideToTray(HWND hwnd) {
    CancelHotkeyCapture(true);
    if (!EnsureTrayIcon(hwnd)) {
        MessageBoxW(hwnd, L"无法创建托盘图标，EasyQR 将保持主窗口打开。", L"EasyQR",
                    MB_ICONERROR | MB_OK);
        return;
    }
    ShowWindow(hwnd, SW_HIDE);
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
            if (lParam == WM_LBUTTONDBLCLK) {
                ShowMainFromTray(hwnd);
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        case kShowMainMessage:
            ShowMainFromTray(hwnd);
            return 0;
        case kQuitForInstallerMessage:
            g_app.quitting = true;
            g_app.hotkeyCapture = AppState::HotkeyCapture::None;
            UnregisterConfiguredHotkeys();
            RemoveTrayIcon();
            DestroyWindow(hwnd);
            return 0;
        case WM_CREATE: {
            g_app.hwnd = hwnd;
            const HICON large = static_cast<HICON>(
                LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                           GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
            const HICON small = static_cast<HICON>(
                LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
            if (large) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
            if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
            ApplyWindowTheme(hwnd, g_app.dark);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintMainWindow(hwnd);
            return 0;
        case WM_SIZE: {
            if (g_app.rt) {
                RECT client{};
                GetClientRect(hwnd, &client);
                g_app.rt->Resize(D2D1::SizeU(
                    static_cast<UINT>(std::max<LONG>(1, client.right - client.left)),
                    static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top))));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DPICHANGED: {
            const UINT newDpi = HIWORD(wParam);
            g_app.dpi = newDpi ? newDpi : 96;
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            RECT client{0, 0, MulDiv(static_cast<int>(kClientWidth), g_app.dpi, 96),
                        MulDiv(static_cast<int>(ClientHeightForView()), g_app.dpi, 96)};
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            AdjustWindowRectExForDpi(&client, style, FALSE, exStyle, g_app.dpi);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, client.right - client.left,
                         client.bottom - client.top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_app.rt) {
                g_app.rt->SetDpi(static_cast<float>(g_app.dpi), static_cast<float>(g_app.dpi));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
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
            SetCursor(LoadCursorW(nullptr, control >= 0 ? IDC_HAND : IDC_ARROW));
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
                SetCursor(LoadCursorW(nullptr, g_app.hoverControl >= 0 ? IDC_HAND : IDC_ARROW));
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
            g_app.hotkeyCapture = AppState::HotkeyCapture::None;
            UnregisterConfiguredHotkeys();
            RemoveTrayIcon();
            DestroyOverlayForExit();
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

void ApplyWindowTheme(HWND hwnd, bool dark) {
    if (!hwnd) return;
    const BOOL useDark = dark ? TRUE : FALSE;
    SetDwmAttribute(hwnd, 20, &useDark, sizeof(useDark));    // DWMWA_USE_IMMERSIVE_DARK_MODE
    int backdrop = 2;                                        // DWMSBT_MAINWINDOW (Mica)
    SetDwmAttribute(hwnd, 38, &backdrop, sizeof(backdrop));  // DWMWA_SYSTEMBACKDROP_TYPE
    int corner = 2;                                          // DWMWCP_ROUND
    SetDwmAttribute(hwnd, 33, &corner, sizeof(corner));      // DWMWA_WINDOW_CORNER_PREFERENCE
}

void RefreshTheme() {
    g_app.systemDark = IsDarkThemePreferred();
    if (g_app.themeMode != ThemeMode::System) return;

    g_app.dark = g_app.systemDark;
    g_app.palette = MakePalette(g_app.dark);
    ApplyWindowTheme(g_app.hwnd, g_app.dark);
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void SetThemeMode(ThemeMode mode) {
    g_app.themeMode = mode;
    if (mode == ThemeMode::System) g_app.systemDark = IsDarkThemePreferred();
    const bool dark = mode == ThemeMode::System ? g_app.systemDark : mode == ThemeMode::Dark;
    g_app.dark = dark;
    g_app.palette = MakePalette(dark);
    SaveThemeMode(mode);
    ApplyWindowTheme(g_app.hwnd, dark);
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_app.instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    bool launchedHidden = false;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int i = 1; i < argumentCount; ++i) {
            if (_wcsicmp(arguments[i], L"--tray") == 0) {
                launchedHidden = true;
                break;
            }
        }
        LocalFree(arguments);
    }

    struct MutexGuard {
        HANDLE handle = nullptr;
        ~MutexGuard() {
            if (handle) CloseHandle(handle);
        }
    } singleInstance;
    singleInstance.handle = CreateMutexW(nullptr, FALSE, L"Local\\EasyQR.SingleInstance");
    if (singleInstance.handle && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(EASYQR_MAIN_CLASS, nullptr);
        if (existing && !launchedHidden) {
            PostMessageW(existing, kShowMainMessage, 0, 0);
        }
        return 0;
    }

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
    g_app.autoStart = IsAutoStartEnabled();
    g_app.fullscreenHotkey = LoadHotkey(true);
    g_app.regionHotkey = LoadHotkey(false);
    g_app.launchedHidden = launchedHidden;
    g_app.dark = g_app.themeMode == ThemeMode::System ? g_app.systemDark
                                                      : g_app.themeMode == ThemeMode::Dark;
    g_app.palette = MakePalette(g_app.dark);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = EASYQR_MAIN_CLASS;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_SHARED));

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"窗口注册失败。", L"EasyQR", MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    const UINT systemDpi = GetDpiForSystem();
    RECT windowRect{0, 0, MulDiv(static_cast<int>(kClientWidth), systemDpi, 96),
                    MulDiv(static_cast<int>(ClientHeightForView()), systemDpi, 96)};
    const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    AdjustWindowRectExForDpi(&windowRect, style, FALSE, WS_EX_APPWINDOW, systemDpi);
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int windowX = work.left + ((work.right - work.left) - windowWidth) / 2;
    const int windowY = work.top + ((work.bottom - work.top) - windowHeight) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, EASYQR_MAIN_CLASS, EASYQR_APP_NAME, style, windowX,
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
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }
    } else {
        ShowWindow(hwnd, showCommand == SW_SHOWMAXIMIZED ? SW_SHOWNORMAL : showCommand);
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

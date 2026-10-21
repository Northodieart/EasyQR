#include "app.h"
#include "overlay.h"
#include "resource.h"
#include "version.h"

#include <cwchar>

AppState g_app;

namespace {

constexpr float kClientWidth = 400.0f;
constexpr float kClientHeight = 240.0f;

enum ControlId : int {
    kControlNone = -1,
    kFullScreen = 0,
    kRegion = 1,
    kSettings = 2,
    kBack = 10,
    kThemeSystem = 11,
    kThemeLight = 12,
    kThemeDark = 13,
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
    return kControlNone;
}

std::vector<int> FocusOrder() {
    if (g_app.view == MainView::Home) return {kFullScreen, kRegion, kSettings};
    return {kBack, kThemeSystem, kThemeLight, kThemeDark};
}

void ResetInteraction() {
    g_app.hoverControl = kControlNone;
    g_app.pressedControl = kControlNone;
    g_app.focusControl = kControlNone;
    g_app.keyboardFocus = false;
}

void SwitchView(MainView view) {
    if (g_app.view == view) return;
    g_app.view = view;
    ResetInteraction();
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
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
    const std::wstring body = index == 0 ? L"扫描整个屏幕的二维码" : L"拖动选择屏幕区域";
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
    DrawTextEx(rt, g_app.dwrite.Get(), L"关于", g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 146.0f, 200.0f, 166.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), L"EasyQR " EASYQR_VERSION_STRING, g_app.fonts.label.Get(),
               D2D1::RectF(24.0f, 168.0f, 260.0f, 188.0f), g_app.brush.Get(), TextAlign::Leading,
               true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.tertiary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"MIT License", g_app.fonts.footer.Get(),
               D2D1::RectF(264.0f, 168.0f, 376.0f, 188.0f), g_app.brush.Get(),
               TextAlign::Trailing, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"截图与识别数据仅在本机内存中处理，不保存任何数据。",
               g_app.fonts.footer.Get(), D2D1::RectF(24.0f, 190.0f, 376.0f, 208.0f),
               g_app.brush.Get(), TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.tertiary);
    DrawTextEx(rt, g_app.dwrite.Get(),
               L"解码：ZXing-C++ + quirc  ·  主题设置仅本次运行有效", g_app.fonts.chipSmall.Get(),
               D2D1::RectF(24.0f, 210.0f, 376.0f, 230.0f), g_app.brush.Get(),
               TextAlign::Leading, true, true);
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
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
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
                        MulDiv(static_cast<int>(kClientHeight), g_app.dpi, 96)};
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
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && g_app.pressedControl != kControlNone) {
                g_app.pressedControl = kControlNone;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
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
    ApplyWindowTheme(g_app.hwnd, dark);
    if (g_app.hwnd) InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_app.instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
    g_app.themeMode = ThemeMode::System;
    g_app.dark = g_app.systemDark;
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
                    MulDiv(static_cast<int>(kClientHeight), systemDpi, 96)};
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
    ShowWindow(hwnd, showCommand == SW_SHOWMAXIMIZED ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(hwnd);

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

#include "app.h"
#include "capture.h"
#include "overlay.h"
#include "version.h"
#include "resource.h"

#include <cwchar>

AppState g_app;

namespace {

constexpr float kClientWidth = 560.0f;
constexpr float kClientHeight = 336.0f;
constexpr float kMargin = 24.0f;
constexpr float kCardTop = 104.0f;
constexpr float kCardWidth = 244.0f;
constexpr float kCardHeight = 168.0f;
constexpr float kCardGap = 24.0f;

const D2D1_RECT_F kCardRects[2] = {
    D2D1::RectF(kMargin, kCardTop, kMargin + kCardWidth, kCardTop + kCardHeight),
    D2D1::RectF(kMargin + kCardWidth + kCardGap, kCardTop,
                kMargin + kCardWidth * 2 + kCardGap, kCardTop + kCardHeight),
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
    if (g_app.rt) return;
    if (!g_app.d2d) return;

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

int HitTestCard(float x, float y) {
    for (int i = 0; i < 2; ++i) {
        if (PointInRect(kCardRects[i], x, y)) return i;
    }
    return -1;
}

void DrawCard(ID2D1HwndRenderTarget* rt, int index, bool keyboardFocused) {
    const D2D1_RECT_F rect = kCardRects[index];
    const bool hovered = g_app.hoverCard == index;
    const bool pressed = g_app.pressedCard == index;

    D2D1_COLOR_F fill = g_app.palette.card;
    if (pressed) {
        fill = g_app.palette.cardPressed;
    } else if (hovered) {
        fill = g_app.palette.cardHover;
    }

    SetBrushColor(g_app.brush.Get(), fill);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 10.0f, 10.0f), g_app.brush.Get());

    SetBrushColor(g_app.brush.Get(),
                  hovered || keyboardFocused ? g_app.palette.borderHover : g_app.palette.border);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, 10.0f, 10.0f), g_app.brush.Get(), 1.0f);

    const float pad = 18.0f;
    const D2D1_RECT_F badge =
        D2D1::RectF(rect.left + pad, rect.top + pad, rect.left + pad + 44.0f, rect.top + pad + 44.0f);
    SetBrushColor(g_app.brush.Get(),
                  hovered ? g_app.palette.accentSoftHover : g_app.palette.accentSoft);
    rt->FillRoundedRectangle(D2D1::RoundedRect(badge, 12.0f, 12.0f), g_app.brush.Get());

    const IconId icon = index == 0 ? IconId::Screen : IconId::Region;
    const D2D1_RECT_F iconRect = D2D1::RectF(badge.left + 9.0f, badge.top + 9.0f,
                                            badge.right - 9.0f, badge.bottom - 9.0f);
    DrawIcon(rt, icon, iconRect, g_app.palette.accent, g_app.brush.Get(), 1.6f);

    SetBrushColor(g_app.brush.Get(),
                  hovered ? g_app.palette.accent : g_app.palette.tertiary);
    const D2D1_RECT_F chevron = D2D1::RectF(rect.right - 38.0f, rect.top + 24.0f,
                                           rect.right - 22.0f, rect.top + 40.0f);
    DrawIcon(rt, IconId::ChevronRight, chevron, hovered ? g_app.palette.accent : g_app.palette.tertiary,
             g_app.brush.Get(), 1.5f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    const std::wstring title = index == 0 ? L"全屏识别" : L"框选识别";
    DrawTextEx(rt, g_app.dwrite.Get(), title, g_app.fonts.cardTitle.Get(),
               D2D1::RectF(rect.left + pad, rect.top + 76.0f, rect.right - pad, rect.top + 104.0f),
               g_app.brush.Get(), TextAlign::Leading, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    const std::wstring body = index == 0
                                  ? L"扫描整个屏幕的二维码\n并选择要打开的网页"
                                  : L"拖动框选屏幕区域\n识别后直接打开网页";
    DrawTextEx(rt, g_app.dwrite.Get(), body, g_app.fonts.cardBody.Get(),
               D2D1::RectF(rect.left + pad, rect.top + 106.0f, rect.right - pad,
                           rect.top + 150.0f),
               g_app.brush.Get(), TextAlign::Leading, false);

    if (keyboardFocused) {
        const D2D1_RECT_F focusRect =
            D2D1::RectF(rect.left + 2.0f, rect.top + 2.0f, rect.right - 2.0f, rect.bottom - 2.0f);
        SetBrushColor(g_app.brush.Get(), g_app.palette.accent);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(focusRect, 8.0f, 8.0f), g_app.brush.Get(), 2.0f);
    }
}

void DrawHeader(ID2D1HwndRenderTarget* rt) {
    DrawAppLogo(rt, D2D1::RectF(kMargin, 22.0f, kMargin + 46.0f, 68.0f), g_app.brush.Get(), 12.0f);

    SetBrushColor(g_app.brush.Get(), g_app.palette.text);
    DrawTextEx(rt, g_app.dwrite.Get(), L"EasyQR", g_app.fonts.appTitle.Get(),
               D2D1::RectF(kMargin + 62.0f, 14.0f, 320.0f, 52.0f), g_app.brush.Get(),
               TextAlign::Leading, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.secondary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"识别屏幕上的二维码，并在浏览器中打开", g_app.fonts.appSubtitle.Get(),
               D2D1::RectF(kMargin + 63.0f, 50.0f, 500.0f, 72.0f), g_app.brush.Get(),
               TextAlign::Leading, true);
}

void DrawFooter(ID2D1HwndRenderTarget* rt) {
    SetBrushColor(g_app.brush.Get(), g_app.palette.footerLine);
    rt->DrawLine(D2D1::Point2F(kMargin, 288.0f), D2D1::Point2F(kClientWidth - kMargin, 288.0f),
                 g_app.brush.Get(), 1.0f);

    IconId icon = IconId::Shield;
    D2D1_COLOR_F iconColor = g_app.palette.tertiary;
    D2D1_COLOR_F textColor = g_app.palette.secondary;
    std::wstring text = L"所有识别均在本机完成，不保存任何图像或数据";

    if (g_app.statusKind == AppState::StatusKind::Info) {
        icon = IconId::Check;
        iconColor = g_app.palette.success;
        textColor = g_app.palette.secondary;
        text = g_app.status;
    } else if (g_app.statusKind == AppState::StatusKind::Error) {
        icon = IconId::Warning;
        iconColor = g_app.palette.danger;
        textColor = g_app.palette.danger;
        text = g_app.status;
    }

    DrawIcon(rt, icon, D2D1::RectF(kMargin, 299.0f, kMargin + 15.0f, 314.0f), iconColor,
             g_app.brush.Get(), 1.3f);

    SetBrushColor(g_app.brush.Get(), textColor);
    DrawTextEx(rt, g_app.dwrite.Get(), text, g_app.fonts.footer.Get(),
               D2D1::RectF(kMargin + 22.0f, 296.0f, kClientWidth - 104.0f, 318.0f),
               g_app.brush.Get(), TextAlign::Leading, true, true);

    SetBrushColor(g_app.brush.Get(), g_app.palette.tertiary);
    DrawTextEx(rt, g_app.dwrite.Get(), L"v" EASYQR_VERSION_STRING, g_app.fonts.footer.Get(),
               D2D1::RectF(kClientWidth - 92.0f, 296.0f, kClientWidth - kMargin, 318.0f),
               g_app.brush.Get(), TextAlign::Trailing, true);
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

    DrawHeader(rt);
    const bool focused = GetFocus() == hwnd;
    for (int i = 0; i < 2; ++i) {
        DrawCard(rt, i, focused && g_app.focusCard == i);
    }
    DrawFooter(rt);

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
            const int card = HitTestCard(x, y);
            if (card != g_app.hoverCard) {
                g_app.hoverCard = card;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            SetCursor(LoadCursorW(nullptr, card >= 0 ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_app.hoverCard != -1) {
                g_app.hoverCard = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            const float scale = static_cast<float>(g_app.dpi) / 96.0f;
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
            const int card = HitTestCard(x, y);
            if (card >= 0) {
                g_app.pressedCard = card;
                g_app.focusCard = card;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const bool wasPressed = g_app.pressedCard >= 0;
            const int pressed = g_app.pressedCard;
            g_app.pressedCard = -1;
            if (GetCapture() == hwnd) ReleaseCapture();

            const float scale = static_cast<float>(g_app.dpi) / 96.0f;
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
            const int card = HitTestCard(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);

            if (wasPressed && card >= 0 && card == pressed) {
                SetStatus(L"", AppState::StatusKind::None);
                StartScan(card == 0 ? ScanMode::Fullscreen : ScanMode::Region);
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, g_app.hoverCard >= 0 ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_KEYDOWN: {
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (wParam == VK_TAB) {
                if (g_app.focusCard < 0) {
                    g_app.focusCard = shift ? 1 : 0;
                } else {
                    g_app.focusCard = 1 - g_app.focusCard;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                g_app.focusCard = wParam == VK_LEFT ? 0 : 1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if ((wParam == VK_RETURN || wParam == VK_SPACE) && g_app.focusCard >= 0) {
                SetStatus(L"", AppState::StatusKind::None);
                StartScan(g_app.focusCard == 0 ? ScanMode::Fullscreen : ScanMode::Region);
                return 0;
            }
            break;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) g_app.pressedCard = -1;
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            DestroyOverlayForExit();
            g_app.hwnd = nullptr;
            DiscardRenderTarget();
            PostQuitMessage(0);
            return 0;
        }
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
    SetDwmAttribute(hwnd, 20, &useDark, sizeof(useDark));  // DWMWA_USE_IMMERSIVE_DARK_MODE
    int backdrop = 2;                                      // DWMSBT_MAINWINDOW (Mica)
    SetDwmAttribute(hwnd, 38, &backdrop, sizeof(backdrop));  // DWMWA_SYSTEMBACKDROP_TYPE
    int corner = 2;                                          // DWMWCP_ROUND
    SetDwmAttribute(hwnd, 33, &corner, sizeof(corner));      // DWMWA_WINDOW_CORNER_PREFERENCE
}

void RefreshTheme() {
    const bool dark = IsDarkThemePreferred();
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

    g_app.dark = IsDarkThemePreferred();
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

#pragma once

#include "render.h"
#include "settings.h"

enum class MainView { Home, Settings };

enum class UpdateState { Idle, Checking, Available, Downloading, UpToDate, Error };

struct AppState {
    enum class StatusKind { None, Info, Error };

    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    bool systemDark = false;
    bool dark = false;
    ThemeMode themeMode = ThemeMode::System;
    MainView view = MainView::Home;

    bool closeToTray = false;
    bool autoStart = false;
    bool launchedHidden = false;
    bool scanReturnToTray = false;
    bool quitting = false;
    bool trayVisible = false;
    bool trayBalloonShown = false;

    HotkeyBinding fullscreenHotkey = HotkeyBinding::DefaultFullscreen();
    HotkeyBinding regionHotkey = HotkeyBinding::DefaultRegion();
    bool hotkeyFullscreenRegistered = false;
    bool hotkeyRegionRegistered = false;
    enum class HotkeyCapture { None, Fullscreen, Region };
    HotkeyCapture hotkeyCapture = HotkeyCapture::None;
    std::wstring settingsNotice;
    bool settingsNoticeError = false;
    Palette palette{};
    FontSet fonts{};

    UpdateState updateState = UpdateState::Idle;
    std::wstring updateMessage;
    std::wstring updateVersion;
    std::wstring updateTag;
    std::thread updateThread;

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    std::vector<uint32_t> glassPixels;
    int glassWidth = 0;
    int glassHeight = 0;
    int glassScreenX = 0;
    int glassScreenY = 0;
    int glassScreenWidth = 0;
    int glassScreenHeight = 0;
    bool glassAvailable = false;

    ComPtr<ID2D1RenderTarget> rt;
    ID2D1HwndRenderTarget* hwndTarget = nullptr;
    ID2D1DeviceContext* d2dContext = nullptr;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Bitmap> glassBitmap;
    ComPtr<ID2D1Bitmap1> targetBitmap;
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<IDCompositionDevice> compositionDevice;
    ComPtr<IDCompositionTarget> compositionTarget;
    ComPtr<IDCompositionVisual> compositionVisual;
    bool compositionMode = false;
    bool backdropEnabled = false;

    UINT dpi = 96;
    int hoverControl = -1;
    int pressedControl = -1;
    int focusControl = -1;
    bool keyboardFocus = false;

    StatusKind statusKind = StatusKind::None;
    std::wstring status;
};

extern AppState g_app;

void RefreshTheme();
void SetThemeMode(ThemeMode mode);
void ApplyWindowTheme(HWND hwnd, bool dark);
void SetStatus(std::wstring text, AppState::StatusKind kind);
void RefreshMainBackdrop();

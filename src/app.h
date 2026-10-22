#pragma once

#include "render.h"
#include "settings.h"

enum class MainView { Home, Settings };

enum class UpdateState { Idle, Checking, Available, Downloading, Launching, UpToDate, Error };

struct AppState {
    enum class StatusKind { None, Info, Error };

    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    bool systemDark = false;
    bool dark = false;
    ThemeMode themeMode = ThemeMode::System;
    GlassMode glassMode = GlassMode::Static;
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

    bool viewAnimationActive = false;
    ULONGLONG viewAnimationStart = 0;
    int viewAnimationStartHeight = 0;
    int viewAnimationTargetHeight = 0;

    struct SliderAnimation {
        bool active = false;
        ULONGLONG start = 0;
        float from = 0.0f;
        float to = 0.0f;
    };
    SliderAnimation themeSlider;
    SliderAnimation glassSlider;

    ComPtr<ID2D1RenderTarget> rt;
    ID2D1HwndRenderTarget* hwndTarget = nullptr;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Bitmap> glassBitmap;
    bool liveSampling = false;
    bool realtimeAlpha = false;  // DWM honours our surface alpha (Win11 24H2+)
    bool dragCapturePending = false;

    UINT dpi = 96;
    int hoverControl = -1;
    int pressedControl = -1;
    int focusControl = -1;
    bool keyboardFocus = false;

    StatusKind statusKind = StatusKind::None;
    std::wstring status;
};

extern AppState g_app;

inline bool GlassEnabled() { return g_app.glassMode != GlassMode::None; }

void RefreshTheme();
void SetThemeMode(ThemeMode mode);
void ApplyWindowTheme(HWND hwnd, bool dark);
void SetStatus(std::wstring text, AppState::StatusKind kind);
void RefreshMainBackdrop();

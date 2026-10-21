#pragma once

#include "render.h"

struct AppState {
    enum class StatusKind { None, Info, Error };

    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    bool dark = false;
    Palette palette{};
    FontSet fonts{};

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;

    UINT dpi = 96;
    int hoverCard = -1;
    int pressedCard = -1;
    int focusCard = -1;

    StatusKind statusKind = StatusKind::None;
    std::wstring status;
};

extern AppState g_app;

void RefreshTheme();
void ApplyWindowTheme(HWND hwnd, bool dark);
void SetStatus(std::wstring text, AppState::StatusKind kind);

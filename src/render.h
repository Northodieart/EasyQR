#pragma once

#include "common.h"

struct Palette {
    D2D1_COLOR_F page;
    D2D1_COLOR_F card;
    D2D1_COLOR_F cardHover;
    D2D1_COLOR_F cardPressed;
    D2D1_COLOR_F border;
    D2D1_COLOR_F borderHover;
    D2D1_COLOR_F text;
    D2D1_COLOR_F secondary;
    D2D1_COLOR_F tertiary;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F accentSoft;
    D2D1_COLOR_F accentSoftHover;
    D2D1_COLOR_F footerLine;
    D2D1_COLOR_F danger;
    D2D1_COLOR_F success;
};

struct FontSet {
    ComPtr<IDWriteTextFormat> appTitle;
    ComPtr<IDWriteTextFormat> appSubtitle;
    ComPtr<IDWriteTextFormat> cardTitle;
    ComPtr<IDWriteTextFormat> cardBody;
    ComPtr<IDWriteTextFormat> footer;
    ComPtr<IDWriteTextFormat> chip;
    ComPtr<IDWriteTextFormat> chipSmall;
    ComPtr<IDWriteTextFormat> messageTitle;
    ComPtr<IDWriteTextFormat> messageBody;
    ComPtr<IDWriteTextFormat> pill;
};

enum class TextAlign { Leading, Center, Trailing };

enum class IconId {
    Screen,
    Region,
    Shield,
    Warning,
    Check,
    Info,
    Link,
    Crosshair,
    ChevronRight,
};

Palette MakePalette(bool dark);
bool CreateFonts(IDWriteFactory* factory, FontSet* fonts);
void SetBrushColor(ID2D1SolidColorBrush* brush, const D2D1_COLOR_F& color);
D2D1_COLOR_F WithAlpha(const D2D1_COLOR_F& color, float alpha);

void DrawTextEx(ID2D1RenderTarget* target, IDWriteFactory* factory, const std::wstring& text,
                IDWriteTextFormat* format, const D2D1_RECT_F& rect, ID2D1Brush* brush,
                TextAlign align = TextAlign::Leading, bool verticalCenter = false,
                bool ellipsis = false, bool wrap = false);
float MeasureTextWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
                       const std::wstring& text, float maxWidth);

void DrawIcon(ID2D1RenderTarget* target, IconId icon, const D2D1_RECT_F& rect,
              const D2D1_COLOR_F& color, ID2D1SolidColorBrush* brush, float stroke = 1.5f);
void DrawAppLogo(ID2D1RenderTarget* target, const D2D1_RECT_F& rect,
                 ID2D1SolidColorBrush* brush, float radius);

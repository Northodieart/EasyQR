#include "render.h"

namespace {

bool FamilyExists(IDWriteFactory* factory, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(collection.Put(), FALSE))) return false;
    if (!collection) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(collection->FindFamilyName(family, &index, &exists)) && exists;
}

std::wstring PickFamily(IDWriteFactory* factory, const wchar_t* preferred,
                        const wchar_t* fallback) {
    if (FamilyExists(factory, preferred)) return preferred;
    if (FamilyExists(factory, fallback)) return fallback;
    return L"Segoe UI";
}

bool MakeFormat(IDWriteFactory* factory, const std::wstring& family, float size,
                DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>* out) {
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = factory->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format.Put());
    if (FAILED(hr) || !format) return false;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    *out = std::move(format);
    return true;
}

D2D1_POINT_2F Pt(const D2D1_RECT_F& r, float sx, float sy, float unit) {
    return D2D1::Point2F(r.left + sx * unit, r.top + sy * unit);
}

D2D1_RECT_F Box(const D2D1_RECT_F& r, float x0, float y0, float x1, float y1, float unit) {
    return D2D1::RectF(r.left + x0 * unit, r.top + y0 * unit, r.left + x1 * unit,
                       r.top + y1 * unit);
}

}  // namespace

Palette MakePalette(bool dark) {
    Palette p{};
    if (!dark) {
        p.page = Rgb(0xF3F3F3);
        p.card = Rgb(0xFFFFFF);
        p.cardHover = Rgb(0xFAFCFF);
        p.cardPressed = Rgb(0xF3F8FE);
        p.border = Rgb(0xE3E3E3);
        p.borderHover = Rgba(0.0f, 0.404f, 0.753f, 0.62f);
        p.text = Rgb(0x1B1B1B);
        p.secondary = Rgb(0x5C5C5C);
        p.tertiary = Rgb(0x8A8A8A);
        p.accent = Rgb(0x0067C0);
        p.accentSoft = Rgba(0.0f, 0.404f, 0.753f, 0.10f);
        p.accentSoftHover = Rgba(0.0f, 0.404f, 0.753f, 0.16f);
        p.footerLine = Rgb(0xE5E5E5);
        p.danger = Rgb(0xC42B1C);
        p.success = Rgb(0x0F7B0F);
    } else {
        p.page = Rgb(0x202020);
        p.card = Rgb(0x2C2C2C);
        p.cardHover = Rgb(0x333333);
        p.cardPressed = Rgb(0x2F2F2F);
        p.border = Rgb(0x3D3D3D);
        p.borderHover = Rgba(0.376f, 0.804f, 1.0f, 0.62f);
        p.text = Rgb(0xFFFFFF);
        p.secondary = Rgb(0xC7C7C7);
        p.tertiary = Rgb(0x9B9B9B);
        p.accent = Rgb(0x60CDFF);
        p.accentSoft = Rgba(0.376f, 0.804f, 1.0f, 0.12f);
        p.accentSoftHover = Rgba(0.376f, 0.804f, 1.0f, 0.20f);
        p.footerLine = Rgb(0x363636);
        p.danger = Rgb(0xFF99A4);
        p.success = Rgb(0x6CCB5F);
    }
    return p;
}

bool CreateFonts(IDWriteFactory* factory, FontSet* fonts) {
    if (!factory || !fonts) return false;
    const std::wstring text = PickFamily(factory, L"Segoe UI Variable Text", L"Segoe UI");
    const std::wstring display = PickFamily(factory, L"Segoe UI Variable Display", text.c_str());

    return MakeFormat(factory, display, 27.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &fonts->appTitle) &&
           MakeFormat(factory, text, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, &fonts->appSubtitle) &&
           MakeFormat(factory, text, 17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &fonts->cardTitle) &&
           MakeFormat(factory, text, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, &fonts->cardBody) &&
           MakeFormat(factory, text, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, &fonts->footer) &&
           MakeFormat(factory, text, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM, &fonts->chip) &&
           MakeFormat(factory, text, 11.0f, DWRITE_FONT_WEIGHT_MEDIUM, &fonts->chipSmall) &&
           MakeFormat(factory, text, 16.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &fonts->messageTitle) &&
           MakeFormat(factory, text, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, &fonts->messageBody) &&
           MakeFormat(factory, text, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &fonts->pill);
}

void SetBrushColor(ID2D1SolidColorBrush* brush, const D2D1_COLOR_F& color) {
    if (brush) brush->SetColor(color);
}

D2D1_COLOR_F WithAlpha(const D2D1_COLOR_F& color, float alpha) {
    D2D1_COLOR_F result = color;
    result.a = alpha;
    return result;
}

void DrawTextEx(ID2D1RenderTarget* target, IDWriteFactory* factory, const std::wstring& text,
                IDWriteTextFormat* format, const D2D1_RECT_F& rect, ID2D1Brush* brush,
                TextAlign align, bool verticalCenter, bool ellipsis, bool wrap) {
    if (!target || !factory || !format || !brush || text.empty()) return;
    const float width = std::max(1.0f, RectWidth(rect));
    const float height = std::max(1.0f, RectHeight(rect));

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
                                         width, height, layout.Put())) ||
        !layout) {
        return;
    }

    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetParagraphAlignment(verticalCenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                 : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    switch (align) {
        case TextAlign::Center:
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            break;
        case TextAlign::Trailing:
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
        default:
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            break;
    }

    ComPtr<IDWriteInlineObject> trimmingSign;
    if (ellipsis) {
        if (SUCCEEDED(factory->CreateEllipsisTrimmingSign(format, trimmingSign.Put())) &&
            trimmingSign) {
            DWRITE_TRIMMING trimming{};
            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            layout->SetTrimming(&trimming, trimmingSign.Get());
        }
    }

    target->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float MeasureTextWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
                       const std::wstring& text, float maxWidth) {
    if (!factory || !format || text.empty()) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
                                         100000.0f, 1000.0f, layout.Put())) ||
        !layout) {
        return 0.0f;
    }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return 0.0f;
    return std::min(maxWidth, metrics.widthIncludingTrailingWhitespace);
}

void DrawIcon(ID2D1RenderTarget* target, IconId icon, const D2D1_RECT_F& rect,
              const D2D1_COLOR_F& color, ID2D1SolidColorBrush* brush, float stroke) {
    if (!target || !brush) return;
    const float unit = std::min(RectWidth(rect), RectHeight(rect)) / 24.0f;
    SetBrushColor(brush, color);
    const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f);
    ComPtr<ID2D1StrokeStyle> strokeStyle;
    if (target) {
        // Stroke styles belong to a factory; create via the render target's factory.
        ComPtr<ID2D1Factory> factory;
        target->GetFactory(factory.Put());
        if (factory) factory->CreateStrokeStyle(props, nullptr, 0, strokeStyle.Put());
    }
    ID2D1StrokeStyle* style = strokeStyle.Get();
    const float sw = std::max(1.0f, stroke);

    auto line = [&](float x0, float y0, float x1, float y1) {
        target->DrawLine(Pt(rect, x0, y0, unit), Pt(rect, x1, y1, unit), brush, sw, style);
    };
    auto arc = [&](float x0, float y0, float x1, float y1, float radius) {
        target->DrawRoundedRectangle(D2D1::RoundedRect(Box(rect, x0, y0, x1, y1, unit), radius * unit,
                                                       radius * unit),
                                     brush, sw, style);
    };
    auto fill = [&](float x0, float y0, float x1, float y1, float radius) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(Box(rect, x0, y0, x1, y1, unit), radius * unit, radius * unit), brush);
    };
    auto dot = [&](float x, float y, float radius) {
        target->FillEllipse(D2D1::Ellipse(Pt(rect, x, y, unit), radius * unit, radius * unit), brush);
    };

    switch (icon) {
        case IconId::Screen:
            arc(2.8f, 4.2f, 21.2f, 17.2f, 2.2f);
            line(8.6f, 20.3f, 15.4f, 20.3f);
            line(12.0f, 17.2f, 12.0f, 20.3f);
            fill(7.2f, 7.6f, 9.6f, 10.0f, 0.5f);
            fill(13.0f, 7.6f, 15.4f, 10.0f, 0.5f);
            fill(7.2f, 12.6f, 9.6f, 15.0f, 0.5f);
            fill(14.6f, 13.2f, 16.4f, 14.9f, 0.4f);
            break;
        case IconId::Region:
            line(3.0f, 9.0f, 3.0f, 4.6f);
            line(3.0f, 4.6f, 7.4f, 4.6f);
            line(16.6f, 4.6f, 21.0f, 4.6f);
            line(21.0f, 4.6f, 21.0f, 9.0f);
            line(3.0f, 15.0f, 3.0f, 19.4f);
            line(3.0f, 19.4f, 7.4f, 19.4f);
            line(16.6f, 19.4f, 21.0f, 19.4f);
            line(21.0f, 19.4f, 21.0f, 15.0f);
            line(12.0f, 9.3f, 12.0f, 14.7f);
            line(9.3f, 12.0f, 14.7f, 12.0f);
            dot(12.0f, 12.0f, 0.9f);
            break;
        case IconId::Shield:
            line(12.0f, 2.6f, 20.0f, 5.5f);
            line(20.0f, 5.5f, 20.0f, 10.8f);
            line(20.0f, 10.8f, 12.0f, 21.3f);
            line(12.0f, 21.3f, 4.0f, 10.8f);
            line(4.0f, 10.8f, 4.0f, 5.5f);
            line(4.0f, 5.5f, 12.0f, 2.6f);
            line(8.4f, 11.8f, 10.9f, 14.3f);
            line(10.9f, 14.3f, 15.6f, 8.9f);
            break;
        case IconId::Warning:
        case IconId::Info:
        case IconId::Check:
            target->DrawEllipse(D2D1::Ellipse(Pt(rect, 12.0f, 12.0f, unit), 9.2f * unit, 9.2f * unit),
                                brush, sw, style);
            if (icon == IconId::Warning) {
                line(12.0f, 7.2f, 12.0f, 13.0f);
                dot(12.0f, 16.7f, 0.95f);
            } else if (icon == IconId::Info) {
                dot(12.0f, 7.6f, 0.95f);
                line(12.0f, 11.0f, 12.0f, 16.8f);
            } else {
                line(7.6f, 12.2f, 10.6f, 15.2f);
                line(10.6f, 15.2f, 16.5f, 8.8f);
            }
            break;
        case IconId::Link:
            arc(3.4f, 8.8f, 14.6f, 20.0f, 1.5f);
            line(12.0f, 3.4f, 20.6f, 3.4f);
            line(20.6f, 3.4f, 20.6f, 12.0f);
            line(20.6f, 3.4f, 11.0f, 13.0f);
            break;
        case IconId::Crosshair:
            target->DrawEllipse(D2D1::Ellipse(Pt(rect, 12.0f, 12.0f, unit), 5.4f * unit, 5.4f * unit),
                                brush, sw, style);
            line(12.0f, 2.8f, 12.0f, 6.0f);
            line(12.0f, 18.0f, 12.0f, 21.2f);
            line(2.8f, 12.0f, 6.0f, 12.0f);
            line(18.0f, 12.0f, 21.2f, 12.0f);
            dot(12.0f, 12.0f, 0.8f);
            break;
        case IconId::ChevronRight:
            line(9.2f, 4.8f, 16.4f, 12.0f);
            line(16.4f, 12.0f, 9.2f, 19.2f);
            break;
    }
}

void DrawAppLogo(ID2D1RenderTarget* target, const D2D1_RECT_F& rect,
                 ID2D1SolidColorBrush* brush, float radius) {
    if (!target || !brush) return;
    const D2D1_COLOR_F brand = Rgb(0x0067C0);
    SetBrushColor(brush, brand);
    target->FillRoundedRectangle(
        D2D1::RoundedRect(rect, radius, radius), brush);

    const float size = std::min(RectWidth(rect), RectHeight(rect));
    const float unit = size / 46.0f;
    const D2D1_COLOR_F white = Rgb(0xFFFFFF);

    auto finder = [&](float x, float y) {
        SetBrushColor(brush, white);
        target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.left + x * unit, rect.top + y * unit,
                                          rect.left + (x + 14.0f) * unit, rect.top + (y + 14.0f) * unit),
                              3.0f * unit, 3.0f * unit),
            brush);
        SetBrushColor(brush, brand);
        target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.left + (x + 2.0f) * unit, rect.top + (y + 2.0f) * unit,
                                          rect.left + (x + 12.0f) * unit, rect.top + (y + 12.0f) * unit),
                              1.8f * unit, 1.8f * unit),
            brush);
        SetBrushColor(brush, white);
        target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.left + (x + 4.5f) * unit, rect.top + (y + 4.5f) * unit,
                                          rect.left + (x + 9.5f) * unit, rect.top + (y + 9.5f) * unit),
                              1.0f * unit, 1.0f * unit),
            brush);
    };

    finder(9.0f, 9.0f);
    finder(23.0f, 9.0f);
    finder(9.0f, 23.0f);

    SetBrushColor(brush, white);
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(rect.left + 24.0f * unit, rect.top + 24.0f * unit,
                                      rect.left + 36.0f * unit, rect.top + 36.0f * unit),
                          1.6f * unit, 1.6f * unit),
        brush);
    SetBrushColor(brush, brand);
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + 30.0f * unit,
                                                   rect.top + 30.0f * unit),
                                      2.0f * unit, 2.0f * unit),
                        brush);
}

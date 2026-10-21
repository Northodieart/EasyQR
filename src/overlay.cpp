#include "overlay.h"

#include "app.h"
#include "capture.h"
#include "qr.h"
#include "url.h"
#include "version.h"

#include <cstring>
#include <new>

namespace {

constexpr float kTopOffset = 20.0f;
constexpr float kChipHeight = 36.0f;
constexpr float kPillHeight = 38.0f;
constexpr UINT kAutoOpenMessage = WM_APP + 1;
constexpr UINT_PTR kAutoOpenTimer = 1;

struct OverlayHit {
    RECT qrPx{};
    std::wstring url;
    std::wstring payload;
    std::wstring format;
    std::wstring label;
    bool isWeb = false;
    D2D1_RECT_F pill{};
};

struct OverlayState {
    ScanMode mode = ScanMode::Fullscreen;
    ScreenFrame frame;
    std::vector<OverlayHit> hits;
    int hover = -1;
    int downHit = -1;
    bool mouseDown = false;
    bool selecting = false;
    bool hasSelection = false;
    POINT selectStart{};
    POINT selectCurrent{};
    RECT selection{};
    bool message = false;
    bool singleAuto = false;
    float uiScale = 1.0f;
    RECT primaryPx{};
    std::wstring messageTitle;
    std::wstring messageBody;
    std::wstring messageHint;
    std::wstring pendingStatus;
    AppState::StatusKind pendingKind = AppState::StatusKind::Error;
    bool trackingMouse = false;
    bool releasingCapture = false;

    int resultHit = -1;
    bool resultFromChooser = false;
    bool resultCopied = false;
    bool resultCopyFailed = false;
    int resultHover = -1;
    int resultPressed = -1;
    std::wstring resultText;
    D2D1_RECT_F resultCard{};
    D2D1_RECT_F copyButton{};
    D2D1_RECT_F backButton{};

    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<ID2D1SolidColorBrush> brush;
};

HWND g_overlayHwnd = nullptr;
std::unique_ptr<OverlayState> g_overlayState;
bool g_classRegistered = false;

const D2D1_COLOR_F kOverlayAccent = Rgb(0x60CDFF);
const D2D1_COLOR_F kOverlayWhite = Rgb(0xFFFFFF);
const D2D1_COLOR_F kOverlaySecondary = Rgba(0.88f, 0.88f, 0.88f, 1.0f);
const D2D1_COLOR_F kOverlayTertiary = Rgba(0.64f, 0.64f, 0.64f, 1.0f);
const D2D1_COLOR_F kOverlayDanger = Rgb(0xFFB4AB);
const D2D1_COLOR_F kOverlaySuccess = Rgb(0x8FE6A0);

std::wstring FormatTitle(const std::wstring& format) {
    if (format == L"QRCode") return L"QR Code";
    if (format == L"MicroQRCode") return L"Micro QR Code";
    if (format == L"RMQRCode" || format == L"rMQRCode") return L"rMQR Code";
    if (format == L"DataMatrix") return L"Data Matrix";
    if (format == L"DataBarExpanded") return L"DataBar Expanded";
    if (format == L"EAN13") return L"EAN-13";
    if (format == L"EAN8") return L"EAN-8";
    if (format == L"UPCA") return L"UPC-A";
    if (format == L"UPCE") return L"UPC-E";
    if (format == L"Code39") return L"Code 39";
    if (format == L"Code93") return L"Code 93";
    if (format == L"Code128") return L"Code 128";
    return format.empty() ? L"未知格式" : format;
}

std::wstring FormatCategory(const std::wstring& format) {
    if (format == L"QRCode" || format == L"MicroQRCode" || format == L"RMQRCode" ||
        format == L"rMQRCode") {
        return L"二维码";
    }
    if (format == L"DataMatrix" || format == L"Aztec" || format == L"PDF417" ||
        format == L"MaxiCode") {
        return L"二维条码";
    }
    return L"条形码";
}

std::wstring SanitizeForDisplay(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            out.push_back(L'\n');
        } else if (c == L'\t') {
            out.push_back(L' ');
        } else if (c < 0x20 || c == 0x7f) {
            out.push_back(L' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::wstring PreviewText(const std::wstring& text, size_t maxLength = 28) {
    const std::wstring clean = SanitizeForDisplay(text);
    std::wstring preview;
    preview.reserve(std::min(clean.size(), maxLength) + 1);
    for (wchar_t c : clean) {
        if (preview.size() >= maxLength) break;
        if (c == L'\n') {
            if (!preview.empty() && preview.back() != L' ') preview.push_back(L' ');
        } else {
            preview.push_back(c);
        }
    }
    if (clean.size() > maxLength) preview += L"…";
    return preview;
}

D2D1_RECT_F DipRect(const RECT& px, float scale) {
    return D2D1::RectF(px.left / scale, px.top / scale, px.right / scale, px.bottom / scale);
}

D2D1_RECT_F Inflate(D2D1_RECT_F rect, float amount) {
    rect.left -= amount;
    rect.top -= amount;
    rect.right += amount;
    rect.bottom += amount;
    return rect;
}

void FillRounded(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& rect,
                 float radius, const D2D1_COLOR_F& color) {
    SetBrushColor(brush, color);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
}

void StrokeRounded(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& rect,
                   float radius, const D2D1_COLOR_F& color, float width) {
    SetBrushColor(brush, color);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush, width);
}

void DrawTopChip(ID2D1RenderTarget* rt, const OverlayState& state, const std::wstring& text,
                 IconId icon) {
    const float textWidth = MeasureTextWidth(g_app.dwrite.Get(), g_app.fonts.chip.Get(), text, 760.0f);
    const float padding = 14.0f;
    const float iconSize = 15.0f;
    const float gap = 8.0f;
    const float width = padding * 2.0f + iconSize + gap + textWidth;
    const float height = kChipHeight;
    const float centerX =
        (state.primaryPx.left + state.primaryPx.right) * 0.5f / state.uiScale;
    const float top = state.primaryPx.top / state.uiScale + kTopOffset;
    float left = centerX - width * 0.5f;
    const float screenWidth = state.frame.width / state.uiScale;
    left = std::max(8.0f, std::min(left, screenWidth - width - 8.0f));
    const D2D1_RECT_F rect = D2D1::RectF(left, top, left + width, top + height);

    FillRounded(rt, state.brush.Get(), D2D1::RectF(rect.left, rect.top + 3.0f, rect.right,
                                                   rect.bottom + 3.0f),
                height * 0.5f, Rgba(0.0f, 0.0f, 0.0f, 0.30f));
    FillRounded(rt, state.brush.Get(), rect, height * 0.5f, Rgba(0.075f, 0.075f, 0.08f, 0.92f));
    StrokeRounded(rt, state.brush.Get(), rect, height * 0.5f, Rgba(1.0f, 1.0f, 1.0f, 0.15f), 1.0f);

    const D2D1_RECT_F iconRect =
        D2D1::RectF(rect.left + padding, rect.top + (height - iconSize) * 0.5f,
                    rect.left + padding + iconSize, rect.top + (height + iconSize) * 0.5f);
    DrawIcon(rt, icon, iconRect, kOverlayAccent, state.brush.Get(), 1.25f);

    SetBrushColor(state.brush.Get(), kOverlayWhite);
    DrawTextEx(rt, g_app.dwrite.Get(), text, g_app.fonts.chip.Get(),
               D2D1::RectF(rect.left + padding + iconSize + gap, rect.top,
                           rect.right - padding, rect.bottom),
               state.brush.Get(), TextAlign::Leading, true, true);
}

void DrawMessageCard(ID2D1RenderTarget* rt, const OverlayState& state) {
    const float cardWidth = 424.0f;
    const float cardHeight = 158.0f;
    const float primaryCenterX =
        (state.primaryPx.left + state.primaryPx.right) * 0.5f / state.uiScale;
    const float primaryCenterY =
        (state.primaryPx.top + state.primaryPx.bottom) * 0.5f / state.uiScale;
    float left = primaryCenterX - cardWidth * 0.5f;
    float top = primaryCenterY - cardHeight * 0.5f;
    const float screenWidth = state.frame.width / state.uiScale;
    const float screenHeight = state.frame.height / state.uiScale;
    left = std::max(16.0f, std::min(left, screenWidth - cardWidth - 16.0f));
    top = std::max(16.0f, std::min(top, screenHeight - cardHeight - 16.0f));
    const D2D1_RECT_F card = D2D1::RectF(left, top, left + cardWidth, top + cardHeight);

    FillRounded(rt, state.brush.Get(),
                D2D1::RectF(card.left + 2.0f, card.top + 7.0f, card.right - 2.0f,
                            card.bottom + 7.0f),
                18.0f, Rgba(0.0f, 0.0f, 0.0f, 0.34f));
    FillRounded(rt, state.brush.Get(), card, 16.0f, Rgba(0.072f, 0.072f, 0.078f, 0.96f));
    StrokeRounded(rt, state.brush.Get(), card, 16.0f, Rgba(1.0f, 1.0f, 1.0f, 0.15f), 1.0f);

    const D2D1_RECT_F iconRect = D2D1::RectF(card.left + cardWidth * 0.5f - 15.0f,
                                            card.top + 20.0f, card.left + cardWidth * 0.5f + 15.0f,
                                            card.top + 50.0f);
    DrawIcon(rt, IconId::Warning, iconRect, kOverlayDanger, state.brush.Get(), 1.7f);

    SetBrushColor(state.brush.Get(), kOverlayWhite);
    DrawTextEx(rt, g_app.dwrite.Get(), state.messageTitle, g_app.fonts.messageTitle.Get(),
               D2D1::RectF(card.left + 20.0f, card.top + 55.0f, card.right - 20.0f,
                           card.top + 91.0f),
               state.brush.Get(), TextAlign::Center, true, true);

    SetBrushColor(state.brush.Get(), kOverlaySecondary);
    DrawTextEx(rt, g_app.dwrite.Get(), state.messageBody, g_app.fonts.messageBody.Get(),
               D2D1::RectF(card.left + 20.0f, card.top + 92.0f, card.right - 20.0f,
                           card.top + 114.0f),
               state.brush.Get(), TextAlign::Center, true, true);

    SetBrushColor(state.brush.Get(), kOverlayTertiary);
    DrawTextEx(rt, g_app.dwrite.Get(), state.messageHint, g_app.fonts.chipSmall.Get(),
               D2D1::RectF(card.left + 20.0f, card.top + 121.0f, card.right - 20.0f,
                           card.top + 143.0f),
               state.brush.Get(), TextAlign::Center, true, true);
}

void DrawPills(ID2D1RenderTarget* rt, OverlayState* state) {
    for (size_t i = 0; i < state->hits.size(); ++i) {
        const OverlayHit& hit = state->hits[i];
        const bool hovered = static_cast<int>(i) == state->hover;
        const bool emphasized = hovered || state->singleAuto;
        const float radius = kPillHeight * 0.5f;

        const D2D1_RECT_F shadow = D2D1::RectF(hit.pill.left, hit.pill.top + 3.0f,
                                               hit.pill.right, hit.pill.bottom + 3.0f);
        FillRounded(rt, state->brush.Get(), shadow, radius, Rgba(0.0f, 0.0f, 0.0f, 0.36f));

        const D2D1_COLOR_F fill =
            emphasized ? Rgb(0x0078D4) : Rgba(0.07f, 0.07f, 0.08f, 0.93f);
        FillRounded(rt, state->brush.Get(), hit.pill, radius, fill);
        StrokeRounded(rt, state->brush.Get(), hit.pill, radius,
                      emphasized ? Rgba(1.0f, 1.0f, 1.0f, 0.35f)
                                 : Rgba(1.0f, 1.0f, 1.0f, 0.22f),
                      1.0f);

        const D2D1_RECT_F iconRect =
            D2D1::RectF(hit.pill.left + 12.0f, hit.pill.top + (kPillHeight - 15.0f) * 0.5f,
                        hit.pill.left + 27.0f, hit.pill.top + (kPillHeight + 15.0f) * 0.5f);
        DrawIcon(rt, hit.isWeb ? IconId::Link : IconId::Info, iconRect, kOverlayWhite,
                 state->brush.Get(), 1.25f);

        SetBrushColor(state->brush.Get(), kOverlayWhite);
        DrawTextEx(rt, g_app.dwrite.Get(), hit.label, g_app.fonts.pill.Get(),
                   D2D1::RectF(hit.pill.left + 35.0f, hit.pill.top, hit.pill.right - 12.0f,
                               hit.pill.bottom),
                   state->brush.Get(), TextAlign::Leading, true, true);
    }
}

void OpenHitAndClose(const OverlayHit& hit);
void DisplayResult(OverlayState* state, int index, bool fromChooser);
void CloseResultPanel(OverlayState* state);
void CopyCurrentResult(OverlayState* state);
int ResultButtonAt(const OverlayState& state, float x, float y);
void DrawResultPanel(ID2D1RenderTarget* rt, OverlayState& state);

void AutoOpenSingle() {
    if (!g_overlayState || !g_overlayState->singleAuto || g_overlayState->hits.empty()) return;
    const OverlayHit hit = g_overlayState->hits.front();
    OpenHitAndClose(hit);
}

void UpdateCursor(HWND hwnd) {
    if (!g_overlayState) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    const OverlayState& state = *g_overlayState;
    LPCTSTR cursor = IDC_ARROW;
    if (state.resultHit >= 0) {
        cursor = state.resultHover >= 0 ? IDC_HAND : IDC_ARROW;
    } else if (state.message) {
        cursor = IDC_ARROW;
    } else if (!state.hits.empty()) {
        cursor = state.hover >= 0 ? IDC_HAND : IDC_ARROW;
    } else if (state.mode == ScanMode::Region) {
        cursor = IDC_CROSS;
    }
    SetCursor(LoadCursorW(nullptr, cursor));
    (void)hwnd;
}

int HitTestPill(const OverlayState& state, float x, float y) {
    int result = -1;
    for (size_t i = 0; i < state.hits.size(); ++i) {
        if (PointInRect(state.hits[i].pill, x, y)) result = static_cast<int>(i);
    }
    return result;
}

void LayoutPills(OverlayState* state) {
    const float screenWidth = state->frame.width / state->uiScale;
    const float screenHeight = state->frame.height / state->uiScale;
    for (OverlayHit& hit : state->hits) {
        const float labelWidth =
            MeasureTextWidth(g_app.dwrite.Get(), g_app.fonts.pill.Get(), hit.label, 240.0f);
        const float width = std::min(300.0f, 47.0f + labelWidth);
        const float centerX = (hit.qrPx.left + hit.qrPx.right) * 0.5f / state->uiScale;
        const float centerY = (hit.qrPx.top + hit.qrPx.bottom) * 0.5f / state->uiScale;
        float left = centerX - width * 0.5f;
        float top = centerY - kPillHeight * 0.5f;
        left = std::max(8.0f, std::min(left, screenWidth - width - 8.0f));
        top = std::max(8.0f, std::min(top, screenHeight - kPillHeight - 8.0f));
        hit.pill = D2D1::RectF(left, top, left + width, top + kPillHeight);
    }
}

void BuildHits(OverlayState* state, const std::vector<QrCode>& codes, int offsetX, int offsetY) {
    for (const QrCode& code : codes) {
        OverlayHit hit;
        hit.qrPx.left = offsetX + code.x;
        hit.qrPx.top = offsetY + code.y;
        hit.qrPx.right = offsetX + code.x + code.width;
        hit.qrPx.bottom = offsetY + code.y + code.height;
        hit.payload = Utf8ToWide(code.payload);
        hit.format = Utf8ToWide(code.format.empty() ? "QRCode" : code.format);
        if (hit.payload.empty()) continue;

        WebUrl web;
        if (TryMakeWebUrl(code.payload, &web)) {
            hit.isWeb = true;
            hit.url = Utf8ToWide(web.url);
            hit.label = Utf8ToWide(web.display);
            if (hit.url.empty() || hit.label.empty()) continue;
        } else {
            hit.isWeb = false;
            hit.label = FormatCategory(hit.format);
            const std::wstring preview = PreviewText(hit.payload);
            if (!preview.empty()) hit.label += L" · " + preview;
        }

        bool duplicate = false;
        for (const OverlayHit& existing : state->hits) {
            if (existing.payload == hit.payload &&
                std::abs(existing.qrPx.left - hit.qrPx.left) < 4 &&
                std::abs(existing.qrPx.top - hit.qrPx.top) < 4) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) state->hits.push_back(std::move(hit));
    }
    LayoutPills(state);
}

void FreeGray(OverlayState* state) {
    std::vector<uint8_t>().swap(state->frame.gray);
}

void ShowMessage(OverlayState* state, const std::wstring& title, const std::wstring& body,
                 const std::wstring& status) {
    state->message = true;
    state->singleAuto = false;
    state->resultHit = -1;
    state->resultFromChooser = false;
    state->messageTitle = title;
    state->messageBody = body;
    state->messageHint = L"点击任意位置或按 Esc 返回";
    state->pendingStatus = status;
    state->pendingKind = AppState::StatusKind::Error;
    state->hover = -1;
}

void DestroyOverlayWindow() {
    HWND hwnd = g_overlayHwnd;
    g_overlayHwnd = nullptr;
    if (hwnd) DestroyWindow(hwnd);
    g_overlayState.reset();
}

void RestoreMainWindow(bool activate) {
    if (!g_app.hwnd) return;
    g_app.hoverControl = -1;
    g_app.pressedControl = -1;
    g_app.focusControl = -1;
    g_app.keyboardFocus = false;
    ShowWindow(g_app.hwnd, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (activate) {
        SetForegroundWindow(g_app.hwnd);
        SetFocus(g_app.hwnd);
    }
    InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void CancelOverlay() {
    DestroyOverlayWindow();
    RestoreMainWindow(true);
}

void DismissMessage() {
    if (!g_overlayState) return;
    const std::wstring status = g_overlayState->pendingStatus;
    const AppState::StatusKind kind = g_overlayState->pendingKind;
    DestroyOverlayWindow();
    if (!status.empty()) SetStatus(status, kind);
    RestoreMainWindow(true);
}

void OpenHitAndClose(const OverlayHit& hit) {
    const std::wstring url = hit.url;
    const std::wstring label = hit.label;
    DestroyOverlayWindow();

    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetStatus(L"无法启动默认浏览器。", AppState::StatusKind::Error);
        RestoreMainWindow(true);
        return;
    }

    SetStatus(L"已在浏览器中打开 " + label, AppState::StatusKind::Info);
    RestoreMainWindow(false);
}

void LayoutResultPanel(OverlayState* state) {
    const float screenWidth = state->frame.width / state->uiScale;
    const float screenHeight = state->frame.height / state->uiScale;
    const float centerX =
        (state->primaryPx.left + state->primaryPx.right) * 0.5f / state->uiScale;
    const float centerY =
        (state->primaryPx.top + state->primaryPx.bottom) * 0.5f / state->uiScale;
    const float cardWidth = std::min(560.0f, screenWidth - 32.0f);
    const float cardHeight = std::min(320.0f, screenHeight - 32.0f);
    float left = centerX - cardWidth * 0.5f;
    float top = centerY - cardHeight * 0.5f;
    left = std::max(16.0f, std::min(left, screenWidth - cardWidth - 16.0f));
    top = std::max(16.0f, std::min(top, screenHeight - cardHeight - 16.0f));
    state->resultCard =
        D2D1::RectF(left, top, left + cardWidth, top + cardHeight);

    const float margin = 24.0f;
    const float buttonHeight = 38.0f;
    const float copyWidth = 132.0f;
    const float backWidth = 112.0f;
    const float gap = 12.0f;
    state->copyButton = D2D1::RectF(state->resultCard.right - margin - copyWidth,
                                    state->resultCard.bottom - margin - buttonHeight,
                                    state->resultCard.right - margin,
                                    state->resultCard.bottom - margin);
    state->backButton = D2D1::RectF(state->copyButton.left - gap - backWidth,
                                    state->copyButton.top, state->copyButton.left - gap,
                                    state->copyButton.bottom);
}

int ResultButtonAt(const OverlayState& state, float x, float y) {
    if (state.resultHit < 0) return -1;
    if (PointInRect(state.copyButton, x, y)) return 0;
    if (PointInRect(state.backButton, x, y)) return 1;
    return -1;
}

void DisplayResult(OverlayState* state, int index, bool fromChooser) {
    if (!state || index < 0 || index >= static_cast<int>(state->hits.size())) return;
    state->resultHit = index;
    state->resultFromChooser = fromChooser;
    state->resultCopied = false;
    state->resultCopyFailed = false;
    state->resultHover = -1;
    state->resultPressed = -1;
    state->hover = -1;
    state->resultText = SanitizeForDisplay(state->hits[static_cast<size_t>(index)].payload);
    LayoutResultPanel(state);
    if (g_overlayHwnd) {
        UpdateCursor(g_overlayHwnd);
        InvalidateRect(g_overlayHwnd, nullptr, FALSE);
    }
}

bool CopyTextToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    std::memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

void CopyCurrentResult(OverlayState* state) {
    if (!state || state->resultHit < 0 ||
        state->resultHit >= static_cast<int>(state->hits.size())) {
        return;
    }
    const std::wstring& payload = state->hits[static_cast<size_t>(state->resultHit)].payload;
    if (CopyTextToClipboard(payload)) {
        state->resultCopied = true;
        state->resultCopyFailed = false;
        SetStatus(L"已复制到剪贴板。", AppState::StatusKind::Info);
    } else {
        state->resultCopied = false;
        state->resultCopyFailed = true;
    }
    if (g_overlayHwnd) InvalidateRect(g_overlayHwnd, nullptr, FALSE);
}

void CloseResultPanel(OverlayState* state) {
    if (!state) return;
    if (state->resultFromChooser) {
        state->resultHit = -1;
        state->resultFromChooser = false;
        state->resultCopied = false;
        state->resultCopyFailed = false;
        state->resultHover = -1;
        state->resultPressed = -1;
        if (g_overlayHwnd) {
            UpdateCursor(g_overlayHwnd);
            InvalidateRect(g_overlayHwnd, nullptr, FALSE);
        }
        return;
    }

    DestroyOverlayWindow();
    RestoreMainWindow(true);
}

void DrawResultPanel(ID2D1RenderTarget* rt, OverlayState& state) {
    if (state.resultHit < 0 || state.resultHit >= static_cast<int>(state.hits.size())) return;
    const OverlayHit& hit = state.hits[static_cast<size_t>(state.resultHit)];
    const D2D1_RECT_F card = state.resultCard;

    FillRounded(rt, state.brush.Get(),
                D2D1::RectF(card.left + 2.0f, card.top + 7.0f, card.right - 2.0f,
                            card.bottom + 7.0f),
                18.0f, Rgba(0.0f, 0.0f, 0.0f, 0.36f));
    FillRounded(rt, state.brush.Get(), card, 16.0f, Rgba(0.072f, 0.072f, 0.078f, 0.97f));
    StrokeRounded(rt, state.brush.Get(), card, 16.0f, Rgba(1.0f, 1.0f, 1.0f, 0.15f), 1.0f);

    SetBrushColor(state.brush.Get(), kOverlayWhite);
    DrawTextEx(rt, g_app.dwrite.Get(), L"识别结果", g_app.fonts.messageTitle.Get(),
               D2D1::RectF(card.left + 24.0f, card.top + 17.0f, card.right - 24.0f,
                           card.top + 47.0f),
               state.brush.Get(), TextAlign::Leading, true, true);

    const std::wstring formatLine = FormatTitle(hit.format) + L" · " + FormatCategory(hit.format);
    SetBrushColor(state.brush.Get(), kOverlayAccent);
    DrawTextEx(rt, g_app.dwrite.Get(), formatLine, g_app.fonts.chip.Get(),
               D2D1::RectF(card.left + 24.0f, card.top + 48.0f, card.right - 24.0f,
                           card.top + 70.0f),
               state.brush.Get(), TextAlign::Leading, true, true);

    const D2D1_RECT_F contentBox = D2D1::RectF(card.left + 24.0f, card.top + 78.0f,
                                               card.right - 24.0f, card.bottom - 74.0f);
    FillRounded(rt, state.brush.Get(), contentBox, 10.0f, Rgba(1.0f, 1.0f, 1.0f, 0.055f));
    StrokeRounded(rt, state.brush.Get(), contentBox, 10.0f, Rgba(1.0f, 1.0f, 1.0f, 0.07f), 1.0f);
    SetBrushColor(state.brush.Get(), kOverlayWhite);
    DrawTextEx(rt, g_app.dwrite.Get(),
               state.resultText.empty() ? L"(空内容)" : state.resultText,
               g_app.fonts.messageBody.Get(),
               D2D1::RectF(contentBox.left + 14.0f, contentBox.top + 12.0f,
                           contentBox.right - 14.0f, contentBox.bottom - 12.0f),
               state.brush.Get(), TextAlign::Leading, false, false, true);

    SetBrushColor(state.brush.Get(),
                  state.resultCopyFailed ? kOverlayDanger
                                         : (state.resultCopied ? kOverlaySuccess : kOverlayTertiary));
    const std::wstring footer =
        state.resultCopyFailed
            ? L"复制失败，请重试"
            : (state.resultCopied
                   ? L"已复制到剪贴板"
                   : (state.resultFromChooser ? L"可复制完整内容，Esc 返回"
                                              : L"可复制完整内容，Esc 关闭"));
    DrawTextEx(rt, g_app.dwrite.Get(), footer, g_app.fonts.chipSmall.Get(),
               D2D1::RectF(card.left + 24.0f, card.bottom - 62.0f, card.right - 300.0f,
                           card.bottom - 28.0f),
               state.brush.Get(), TextAlign::Leading, true, true);

    // Back / close button.
    {
        const bool hovered = state.resultHover == 1;
        const bool pressed = state.resultPressed == 1;
        const D2D1_COLOR_F fill = pressed ? Rgba(0.18f, 0.18f, 0.19f, 0.98f)
                                         : (hovered ? Rgba(0.16f, 0.16f, 0.17f, 0.98f)
                                                    : Rgba(0.11f, 0.11f, 0.12f, 0.98f));
        FillRounded(rt, state.brush.Get(), state.backButton, 10.0f, fill);
        StrokeRounded(rt, state.brush.Get(), state.backButton, 10.0f,
                      hovered ? Rgba(1.0f, 1.0f, 1.0f, 0.32f) : Rgba(1.0f, 1.0f, 1.0f, 0.16f),
                      1.0f);
        SetBrushColor(state.brush.Get(), kOverlayWhite);
        DrawTextEx(rt, g_app.dwrite.Get(), state.resultFromChooser ? L"返回" : L"关闭",
                   g_app.fonts.pill.Get(), state.backButton, state.brush.Get(),
                   TextAlign::Center, true, true);
    }

    // Copy button.
    {
        const bool hovered = state.resultHover == 0;
        const bool pressed = state.resultPressed == 0;
        D2D1_COLOR_F fill = pressed ? Rgb(0x0067C0) : Rgb(0x0078D4);
        D2D1_COLOR_F border = hovered ? Rgba(1.0f, 1.0f, 1.0f, 0.45f)
                                      : Rgba(1.0f, 1.0f, 1.0f, 0.22f);
        D2D1_COLOR_F text = kOverlayWhite;
        std::wstring label = L"复制内容";
        if (state.resultCopied) {
            fill = Rgba(0.16f, 0.25f, 0.18f, 0.98f);
            border = Rgba(0.56f, 0.90f, 0.63f, 0.55f);
            text = kOverlaySuccess;
            label = L"已复制";
        } else if (state.resultCopyFailed) {
            fill = Rgba(0.22f, 0.12f, 0.12f, 0.98f);
            border = Rgba(1.0f, 0.70f, 0.67f, 0.55f);
            text = kOverlayDanger;
            label = L"复制失败";
        }
        FillRounded(rt, state.brush.Get(), state.copyButton, 10.0f, fill);
        StrokeRounded(rt, state.brush.Get(), state.copyButton, 10.0f, border, 1.0f);
        SetBrushColor(state.brush.Get(), text);
        DrawTextEx(rt, g_app.dwrite.Get(), label, g_app.fonts.pill.Get(), state.copyButton,
                   state.brush.Get(), TextAlign::Center, true, true);
    }
}

void PrepareFullscreenResults(OverlayState* state) {
    state->hits.clear();
    state->message = false;
    state->singleAuto = false;
    state->resultHit = -1;
    state->resultFromChooser = false;

    const std::vector<QrCode> codes =
        DecodeQrCodes(state->frame.gray.data(), state->frame.width, state->frame.height);
    BuildHits(state, codes, 0, 0);
    FreeGray(state);

    if (state->hits.empty()) {
        ShowMessage(state, L"未识别到可识别内容",
                    L"请将二维码或条形码完整、清晰地显示在屏幕上",
                    L"未识别到可识别内容。");
        return;
    }

    if (state->hits.size() == 1) {
        if (state->hits.front().isWeb) {
            state->singleAuto = true;
        } else {
            DisplayResult(state, 0, false);
        }
    } else {
        state->hover = -1;
    }
}

void FinalizeRegion(const RECT& selection) {
    if (!g_overlayState) return;
    OverlayState& state = *g_overlayState;

    const int width = selection.right - selection.left;
    const int height = selection.bottom - selection.top;
    if (width < 8 || height < 8) {
        state.hasSelection = false;
        InvalidateRect(g_overlayHwnd, nullptr, FALSE);
        return;
    }

    state.hasSelection = true;
    if (g_overlayHwnd) {
        InvalidateRect(g_overlayHwnd, nullptr, FALSE);
        UpdateWindow(g_overlayHwnd);
    }

    std::vector<uint8_t> crop;
    try {
        crop.resize(static_cast<size_t>(width) * height);
    } catch (const std::bad_alloc&) {
        FreeGray(&state);
        ShowMessage(&state, L"内存不足", L"选区过大，无法完成识别",
                    L"内存不足，无法完成识别。");
        if (g_overlayHwnd) {
            UpdateCursor(g_overlayHwnd);
            InvalidateRect(g_overlayHwnd, nullptr, FALSE);
        }
        return;
    }

    for (int row = 0; row < height; ++row) {
        const uint8_t* src = state.frame.gray.data() +
                             static_cast<size_t>(selection.top + row) * state.frame.width +
                             selection.left;
        std::memcpy(crop.data() + static_cast<size_t>(row) * width, src,
                    static_cast<size_t>(width));
    }

    const std::vector<QrCode> codes = DecodeQrCodes(crop.data(), width, height);
    FreeGray(&state);
    BuildHits(&state, codes, selection.left, selection.top);
    state.hasSelection = false;
    state.selecting = false;
    state.resultHit = -1;

    if (state.hits.size() == 1) {
        if (state.hits.front().isWeb) {
            const OverlayHit hit = state.hits.front();
            OpenHitAndClose(hit);
        } else {
            DisplayResult(&state, 0, false);
            if (g_overlayHwnd) {
                UpdateCursor(g_overlayHwnd);
                InvalidateRect(g_overlayHwnd, nullptr, FALSE);
                UpdateWindow(g_overlayHwnd);
            }
        }
        return;
    }

    if (state.hits.size() > 1) {
        state.hover = -1;
        if (g_overlayHwnd) {
            UpdateCursor(g_overlayHwnd);
            InvalidateRect(g_overlayHwnd, nullptr, FALSE);
            UpdateWindow(g_overlayHwnd);
        }
        return;
    }

    ShowMessage(&state, L"未识别到可识别内容",
                L"请将内容完整、清晰地框选在区域内",
                L"未识别到可识别内容。");
    if (g_overlayHwnd) {
        UpdateCursor(g_overlayHwnd);
        InvalidateRect(g_overlayHwnd, nullptr, FALSE);
    }
}

bool CreateOverlayTargets(HWND hwnd) {
    if (!g_overlayState) return false;
    OverlayState& state = *g_overlayState;

    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
    const UINT height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);

    HRESULT hr = g_app.d2d->CreateHwndRenderTarget(
        properties, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)),
        state.rt.Put());
    if (FAILED(hr) || !state.rt) return false;
    state.rt->SetDpi(96.0f, 96.0f);
    state.rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    state.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    hr = state.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), state.brush.Put());
    if (FAILED(hr) || !state.brush) return false;

    const D2D1_BITMAP_PROPERTIES bitmapProperties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    hr = state.rt->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(state.frame.width), static_cast<UINT32>(state.frame.height)),
        state.frame.bgra.data(), static_cast<UINT32>(state.frame.width) * 4u, bitmapProperties,
        state.bitmap.Put());
    return SUCCEEDED(hr) && state.bitmap;
}

void EnsureOverlayTargets(HWND hwnd) {
    if (!g_overlayState) return;
    OverlayState& state = *g_overlayState;
    if (state.rt && state.bitmap && state.brush) return;
    state.rt.Reset();
    state.bitmap.Reset();
    state.brush.Reset();
    CreateOverlayTargets(hwnd);
}

void PaintOverlay(HWND hwnd) {
    if (!g_overlayState) return;
    PAINTSTRUCT ps{};
    BeginPaint(hwnd, &ps);
    EnsureOverlayTargets(hwnd);
    OverlayState& state = *g_overlayState;
    if (!state.rt || !state.bitmap || !state.brush) {
        EndPaint(hwnd, &ps);
        return;
    }

    ID2D1HwndRenderTarget* rt = state.rt.Get();
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f));
    rt->DrawBitmap(state.bitmap.Get(),
                   D2D1::RectF(0.0f, 0.0f, static_cast<float>(state.frame.width),
                               static_cast<float>(state.frame.height)),
                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    const bool regionSelect = state.mode == ScanMode::Region && state.hits.empty() && !state.message;
    if (!state.singleAuto) {
        const float dimAlpha = state.message ? 0.56f : (regionSelect ? 0.46f : 0.54f);
        SetBrushColor(state.brush.Get(), Rgba(0.0f, 0.0f, 0.0f, dimAlpha));
        rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, static_cast<float>(state.frame.width),
                                     static_cast<float>(state.frame.height)),
                          state.brush.Get());
    }

    if (regionSelect && (state.selecting || state.hasSelection)) {
        const RECT& sel = state.selection;
        if (sel.right > sel.left && sel.bottom > sel.top) {
            const D2D1_RECT_F destination =
                D2D1::RectF(static_cast<float>(sel.left), static_cast<float>(sel.top),
                            static_cast<float>(sel.right), static_cast<float>(sel.bottom));
            const D2D1_RECT_F source = destination;
            rt->DrawBitmap(state.bitmap.Get(), destination, 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &source);
        }
    }

    rt->SetTransform(D2D1::Matrix3x2F::Scale(state.uiScale, state.uiScale));

    if (state.resultHit >= 0) {
        DrawResultPanel(rt, state);
    } else if (regionSelect) {
        if (state.selecting || state.hasSelection) {
            const RECT& sel = state.selection;
            if (sel.right > sel.left && sel.bottom > sel.top) {
                const D2D1_RECT_F selDip = DipRect(sel, state.uiScale);
                StrokeRounded(rt, state.brush.Get(), selDip, 0.0f, Rgba(0.0f, 0.0f, 0.0f, 0.35f),
                              3.0f);
                StrokeRounded(rt, state.brush.Get(), selDip, 0.0f, Rgba(1.0f, 1.0f, 1.0f, 0.95f),
                              1.35f);

                const int w = sel.right - sel.left;
                const int h = sel.bottom - sel.top;
                const std::wstring sizeText = std::to_wstring(w) + L" × " + std::to_wstring(h);
                const float textWidth = MeasureTextWidth(
                    g_app.dwrite.Get(), g_app.fonts.chipSmall.Get(), sizeText, 200.0f);
                const float chipWidth = textWidth + 16.0f;
                const float chipHeight = 24.0f;
                float chipLeft = selDip.left;
                float chipTop = selDip.top - chipHeight - 8.0f;
                if (chipTop < 6.0f) chipTop = selDip.top + 8.0f;
                const float screenWidth = state.frame.width / state.uiScale;
                chipLeft = std::max(6.0f, std::min(chipLeft, screenWidth - chipWidth - 6.0f));
                const D2D1_RECT_F chip = D2D1::RectF(chipLeft, chipTop, chipLeft + chipWidth,
                                                     chipTop + chipHeight);
                FillRounded(rt, state.brush.Get(), chip, 6.0f, Rgba(0.08f, 0.08f, 0.08f, 0.88f));
                SetBrushColor(state.brush.Get(), kOverlayWhite);
                DrawTextEx(rt, g_app.dwrite.Get(), sizeText, g_app.fonts.chipSmall.Get(), chip,
                           state.brush.Get(), TextAlign::Center, true, true);
            }
        }

        if (!state.selecting) {
            DrawTopChip(rt, state, L"拖动框选识别区域  ·  Esc 取消", IconId::Crosshair);
        }
    } else if (state.message) {
        DrawMessageCard(rt, state);
    } else {
        for (const OverlayHit& hit : state.hits) {
            const D2D1_RECT_F qrRect = DipRect(hit.qrPx, state.uiScale);
            const D2D1_RECT_F outline = Inflate(qrRect, 6.0f);
            StrokeRounded(rt, state.brush.Get(), outline, 10.0f, Rgba(0.376f, 0.804f, 1.0f, 0.18f),
                          5.0f);
            StrokeRounded(rt, state.brush.Get(), outline, 10.0f, kOverlayAccent, 1.4f);
        }
        DrawPills(rt, &state);

        if (state.singleAuto) {
            DrawTopChip(rt, state, L"已识别可打开的网页，正在打开浏览器…", IconId::Link);
        } else if (state.mode == ScanMode::Fullscreen) {
            std::wstring hint = L"找到 ";
            hint += std::to_wstring(state.hits.size());
            hint += L" 个可识别内容  ·  点击按钮打开或查看  ·  Esc 退出";
            DrawTopChip(rt, state, hint, IconId::Link);
        } else {
            DrawTopChip(rt, state, L"点击按钮打开或查看内容  ·  Esc 退出", IconId::Link);
        }
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    const HRESULT hr = rt->EndDraw();
    EndPaint(hwnd, &ps);

    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        state.bitmap.Reset();
        state.brush.Reset();
        state.rt.Reset();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void UpdateSelectionFromPoint(OverlayState* state) {
    const int x0 = ClampInt(state->selectStart.x, 0, state->frame.width);
    const int y0 = ClampInt(state->selectStart.y, 0, state->frame.height);
    const int x1 = ClampInt(state->selectCurrent.x, 0, state->frame.width);
    const int y1 = ClampInt(state->selectCurrent.y, 0, state->frame.height);
    state->selection.left = std::min(x0, x1);
    state->selection.top = std::min(y0, y1);
    state->selection.right = std::max(x0, x1);
    state->selection.bottom = std::max(y0, y1);
}

void UpdateHover(HWND hwnd, OverlayState* state, POINT point) {
    const float x = point.x / state->uiScale;
    const float y = point.y / state->uiScale;
    if (state->resultHit >= 0) {
        const int hover = ResultButtonAt(*state, x, y);
        if (hover != state->resultHover) {
            state->resultHover = hover;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        UpdateCursor(hwnd);
        return;
    }
    if (!state->hits.empty()) {
        const int hover = HitTestPill(*state, x, y);
        if (hover != state->hover) {
            state->hover = hover;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    UpdateCursor(hwnd);
}

void RegisterOverlayClass() {
    if (g_classRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
        switch (message) {
            case WM_CREATE: {
                g_overlayHwnd = hwnd;
                BOOL disableTransitions = TRUE;
                DwmSetWindowAttribute(hwnd, 3, &disableTransitions, sizeof(disableTransitions));
                if (!CreateOverlayTargets(hwnd)) return -1;
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
                PaintOverlay(hwnd);
                return 0;
            case WM_MOUSEACTIVATE:
                return MA_ACTIVATE;
            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT) {
                    UpdateCursor(hwnd);
                    return TRUE;
                }
                break;
            case WM_MOUSEMOVE: {
                if (!g_overlayState) return 0;
                OverlayState& state = *g_overlayState;
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (state.selecting) {
                    state.selectCurrent = point;
                    UpdateSelectionFromPoint(&state);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                UpdateHover(hwnd, &state, point);
                if (!state.trackingMouse) {
                    TRACKMOUSEEVENT track{};
                    track.cbSize = sizeof(track);
                    track.dwFlags = TME_LEAVE;
                    track.hwndTrack = hwnd;
                    TrackMouseEvent(&track);
                    state.trackingMouse = true;
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                if (g_overlayState) {
                    g_overlayState->trackingMouse = false;
                    const bool changed = g_overlayState->hover != -1 ||
                                         g_overlayState->resultHover != -1;
                    g_overlayState->hover = -1;
                    g_overlayState->resultHover = -1;
                    if (changed) InvalidateRect(hwnd, nullptr, FALSE);
                    UpdateCursor(hwnd);
                }
                return 0;
            case WM_LBUTTONDOWN: {
                if (!g_overlayState) return 0;
                OverlayState& state = *g_overlayState;
                SetFocus(hwnd);
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                state.mouseDown = true;
                if (state.resultHit >= 0) {
                    state.resultPressed =
                        ResultButtonAt(state, point.x / state.uiScale, point.y / state.uiScale);
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (state.message) {
                    state.downHit = -2;
                } else if (!state.hits.empty()) {
                    state.downHit = HitTestPill(state, point.x / state.uiScale,
                                                point.y / state.uiScale);
                    SetCapture(hwnd);
                } else if (state.mode == ScanMode::Region) {
                    state.selecting = true;
                    state.hasSelection = false;
                    state.selectStart = point;
                    state.selectCurrent = point;
                    UpdateSelectionFromPoint(&state);
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                if (!g_overlayState) return 0;
                OverlayState& state = *g_overlayState;
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const bool wasDown = state.mouseDown;
                state.mouseDown = false;
                if (GetCapture() == hwnd) {
                    state.releasingCapture = true;
                    ReleaseCapture();
                    state.releasingCapture = false;
                }

                if (state.resultHit >= 0) {
                    const int button =
                        ResultButtonAt(state, point.x / state.uiScale, point.y / state.uiScale);
                    const int pressed = state.resultPressed;
                    state.resultPressed = -1;
                    if (button >= 0 && button == pressed) {
                        if (button == 0) {
                            CopyCurrentResult(&state);
                        } else {
                            CloseResultPanel(&state);
                        }
                    } else {
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }

                if (state.message) {
                    if (wasDown) DismissMessage();
                    return 0;
                }

                if (state.selecting) {
                    state.selecting = false;
                    state.selectCurrent = point;
                    UpdateSelectionFromPoint(&state);
                    const RECT selection = state.selection;
                    if (selection.right - selection.left < 8 ||
                        selection.bottom - selection.top < 8) {
                        state.hasSelection = false;
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    FinalizeRegion(selection);
                    return 0;
                }

                if (!state.hits.empty()) {
                    const int hitIndex = HitTestPill(state, point.x / state.uiScale,
                                                     point.y / state.uiScale);
                    if (wasDown && hitIndex >= 0 && hitIndex == state.downHit) {
                        if (state.hits[static_cast<size_t>(hitIndex)].isWeb) {
                            const OverlayHit hit = state.hits[static_cast<size_t>(hitIndex)];
                            OpenHitAndClose(hit);
                        } else {
                            DisplayResult(&state, hitIndex, true);
                        }
                    }
                }
                return 0;
            }
            case WM_RBUTTONUP:
                if (!g_overlayState) return 0;
                if (g_overlayState->resultHit >= 0) {
                    CloseResultPanel(g_overlayState.get());
                } else if (g_overlayState->message) {
                    DismissMessage();
                } else {
                    CancelOverlay();
                }
                return 0;
            case WM_KEYDOWN: {
                if (!g_overlayState) return 0;
                OverlayState& state = *g_overlayState;
                if (state.resultHit >= 0) {
                    if (wParam == VK_ESCAPE) {
                        CloseResultPanel(&state);
                    } else if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                        CopyCurrentResult(&state);
                    }
                    return 0;
                }
                if (wParam == VK_ESCAPE) {
                    if (state.message) {
                        DismissMessage();
                    } else {
                        CancelOverlay();
                    }
                    return 0;
                }
                if (wParam == VK_TAB && !state.hits.empty()) {
                    const int count = static_cast<int>(state.hits.size());
                    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (state.hover < 0) {
                        state.hover = shift ? count - 1 : 0;
                    } else {
                        state.hover = shift ? (state.hover + count - 1) % count
                                            : (state.hover + 1) % count;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    UpdateCursor(hwnd);
                    return 0;
                }
                if ((wParam == VK_RETURN || wParam == VK_SPACE) && !state.hits.empty() &&
                    state.hover >= 0 && state.hover < static_cast<int>(state.hits.size())) {
                    if (state.hits[static_cast<size_t>(state.hover)].isWeb) {
                        const OverlayHit hit = state.hits[static_cast<size_t>(state.hover)];
                        OpenHitAndClose(hit);
                    } else {
                        DisplayResult(&state, state.hover, true);
                    }
                    return 0;
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (g_overlayState && !g_overlayState->releasingCapture) {
                    g_overlayState->selecting = false;
                    g_overlayState->mouseDown = false;
                    g_overlayState->resultPressed = -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_DISPLAYCHANGE:
                if (g_overlayHwnd == hwnd) CancelOverlay();
                return 0;
            case WM_CLOSE:
                if (g_overlayState && g_overlayState->message) {
                    DismissMessage();
                } else {
                    CancelOverlay();
                }
                return 0;
            case WM_TIMER:
                if (wParam == kAutoOpenTimer) {
                    KillTimer(hwnd, kAutoOpenTimer);
                    AutoOpenSingle();
                }
                return 0;
            case kAutoOpenMessage:
                AutoOpenSingle();
                return 0;
            case WM_DESTROY:
                if (g_overlayState) {
                    g_overlayState->bitmap.Reset();
                    g_overlayState->brush.Reset();
                    g_overlayState->rt.Reset();
                }
                if (g_overlayHwnd == hwnd) g_overlayHwnd = nullptr;
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    };
    wc.hInstance = g_app.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = EASYQR_OVERLAY_CLASS;
    if (RegisterClassExW(&wc)) g_classRegistered = true;
}

void FailStart(const std::wstring& status, bool detailedError, const std::wstring& detail) {
    if (detailedError) OutputDebugStringW(detail.c_str());
    DestroyOverlayWindow();
    if (g_app.hwnd) {
        ShowWindow(g_app.hwnd, SW_SHOW);
        SetForegroundWindow(g_app.hwnd);
        SetFocus(g_app.hwnd);
        SetStatus(status, AppState::StatusKind::Error);
        InvalidateRect(g_app.hwnd, nullptr, FALSE);
    }
}

RECT PrimaryMonitorRectRelativeTo(const ScreenFrame& frame) {
    RECT primary{0, 0, frame.width, frame.height};
    POINT origin{0, 0};
    const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        primary.left = info.rcMonitor.left - frame.x;
        primary.top = info.rcMonitor.top - frame.y;
        primary.right = info.rcMonitor.right - frame.x;
        primary.bottom = info.rcMonitor.bottom - frame.y;
    }
    return primary;
}

}  // namespace

void StartScan(ScanMode mode) {
    if (g_overlayState || !g_app.hwnd) return;

    g_app.hoverControl = -1;
    g_app.pressedControl = -1;
    g_app.keyboardFocus = false;

    ShowWindow(g_app.hwnd, SW_HIDE);
    DwmFlush();
    Sleep(180);

    try {
        g_overlayState = std::make_unique<OverlayState>();
    } catch (const std::bad_alloc&) {
        FailStart(L"内存不足，无法开始截图。", false, L"");
        return;
    }

    OverlayState& state = *g_overlayState;
    state.mode = mode;
    std::wstring captureError;
    if (!CaptureScreen(&state.frame, &captureError)) {
        FailStart(L"无法捕获屏幕，请重试。", true, captureError);
        return;
    }

    state.uiScale = std::max(1.0f, static_cast<float>(DpiOfWindow(g_app.hwnd)) / 96.0f);
    state.primaryPx = PrimaryMonitorRectRelativeTo(state.frame);

    if (mode == ScanMode::Fullscreen) {
        PrepareFullscreenResults(&state);
    }

    RegisterOverlayClass();
    if (!g_classRegistered) {
        FailStart(L"无法创建全屏截图界面。", false, L"");
        return;
    }

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (state.singleAuto) exStyle |= WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;

    HWND hwnd = CreateWindowExW(
        exStyle, EASYQR_OVERLAY_CLASS, L"EasyQR 屏幕识别", WS_POPUP, state.frame.x, state.frame.y,
        state.frame.width, state.frame.height, nullptr, nullptr, g_app.instance, nullptr);
    if (!hwnd) {
        FailStart(L"无法创建全屏截图界面。", false, L"");
        return;
    }

    if (state.singleAuto) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(hwnd);
        if (SetTimer(hwnd, kAutoOpenTimer, 500, nullptr) == 0) {
            PostMessageW(hwnd, kAutoOpenMessage, 0, 0);
        }
    } else {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        if (mode == ScanMode::Region) SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        UpdateWindow(hwnd);
    }
}

void DestroyOverlayForExit() {
    DestroyOverlayWindow();
}

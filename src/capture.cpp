#include "capture.h"

#include <cstring>
#include <limits>

namespace {

std::wstring LastErrorText(const wchar_t* prefix) {
    const DWORD code = GetLastError();
    wchar_t buffer[512] = {};
    if (code) {
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                       buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
    }
    std::wstring text(prefix);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
    const std::wstring detail(buffer);
    if (!detail.empty()) text += L"\n" + detail;
    return text;
}

}  // namespace

bool CaptureScreen(ScreenFrame* frame, std::wstring* error) {
    if (!frame) return false;

    ScreenFrame result;
    result.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    result.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    result.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    result.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (result.width <= 0 || result.height <= 0) {
        if (error) *error = L"无法获取屏幕尺寸。";
        return false;
    }

    const uint64_t pixel_count =
        static_cast<uint64_t>(result.width) * static_cast<uint64_t>(result.height);
    if (pixel_count > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        if (error) *error = L"屏幕尺寸过大，无法完成截图。";
        return false;
    }

    HDC screen_dc = GetDC(nullptr);
    if (!screen_dc) {
        if (error) *error = LastErrorText(L"无法访问屏幕。");
        return false;
    }

    HDC memory_dc = CreateCompatibleDC(screen_dc);
    if (!memory_dc) {
        if (error) *error = LastErrorText(L"无法创建截屏缓冲区。");
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = result.width;
    info.bmiHeader.biHeight = -result.height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (error) *error = LastErrorText(L"内存不足，无法创建屏幕快照。");
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        if (bitmap) DeleteObject(bitmap);
        return false;
    }

    HGDIOBJ previous = SelectObject(memory_dc, bitmap);
    BOOL copied = BitBlt(memory_dc, 0, 0, result.width, result.height, screen_dc, result.x,
                         result.y, SRCCOPY | CAPTUREBLT);
    if (!copied) {
        copied = BitBlt(memory_dc, 0, 0, result.width, result.height, screen_dc, result.x,
                        result.y, SRCCOPY);
    }
    GdiFlush();

    if (!copied) {
        if (error) *error = LastErrorText(L"屏幕捕获失败，可能当前画面受保护。");
        SelectObject(memory_dc, previous);
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    try {
        result.bgra.resize(static_cast<size_t>(pixel_count));
        std::memcpy(result.bgra.data(), bits, static_cast<size_t>(pixel_count) * sizeof(uint32_t));
        result.gray.resize(static_cast<size_t>(pixel_count));

        const uint32_t* pixels = result.bgra.data();
        uint8_t* gray = result.gray.data();
        for (size_t i = 0; i < result.bgra.size(); ++i) {
            const uint32_t pixel = pixels[i];
            const uint32_t b = pixel & 0xffu;
            const uint32_t g = (pixel >> 8) & 0xffu;
            const uint32_t r = (pixel >> 16) & 0xffu;
            gray[i] = static_cast<uint8_t>((r * 77u + g * 150u + b * 29u) >> 8);
        }
    } catch (const std::bad_alloc&) {
        if (error) *error = L"内存不足，无法保存屏幕快照。";
        SelectObject(memory_dc, previous);
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    SelectObject(memory_dc, previous);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);

    *frame = std::move(result);
    return true;
}

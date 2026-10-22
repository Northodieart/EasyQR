#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    ~ComPtr() { Reset(); }

    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    T** Put() {
        Reset();
        return &ptr_;
    }

    void Attach(T* ptr) {
        Reset();
        ptr_ = ptr;
    }

    T* Detach() {
        T* result = ptr_;
        ptr_ = nullptr;
        return result;
    }

    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

inline D2D1_COLOR_F Rgba(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r, g, b, a);
}

inline D2D1_COLOR_F Rgb(UINT32 rgb, float alpha = 1.0f) {
    return D2D1::ColorF(rgb, alpha);
}

inline D2D1_RECT_F MakeRect(float left, float top, float right, float bottom) {
    return D2D1::RectF(left, top, right, bottom);
}

inline float RectWidth(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float RectHeight(const D2D1_RECT_F& r) { return r.bottom - r.top; }

inline UINT DpiOfWindow(HWND hwnd) {
    UINT dpi = GetDpiForWindow(hwnd);
    return dpi ? dpi : 96;
}

inline bool PointInRect(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

inline int ClampInt(int v, int low, int high) {
    return v < low ? low : (v > high ? high : v);
}

std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

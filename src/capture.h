#pragma once

#include "common.h"

struct ScreenFrame {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    // Top-down BGRA pixels, used once to create the overlay bitmap.
    std::vector<uint32_t> bgra;
    // Top-down 8-bit grayscale pixels used for QR detection.
    std::vector<uint8_t> gray;
};

// Captures the whole virtual desktop into memory. Never writes a file.
// captureLayered adds CAPTUREBLT; it forces a software cursor path that makes
// the pointer flicker, so only the QR scan needs it.
bool CaptureScreen(ScreenFrame* frame, std::wstring* error, bool includeGray = true,
                   bool captureLayered = true);

// Captures a rectangle of the virtual desktop (virtual-desktop coordinates) as
// BGRA only. The live glass samples the area around the window rather than the
// whole desktop: the grab is what sets the frame rate, and the area is the only
// thing about it the glass can control.
bool CaptureScreenRect(const RECT& bounds, ScreenFrame* frame, std::wstring* error);

#pragma once

#include "common.h"

struct ScreenFrame {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    // Size of the desktop region the pixels stand for. Equal to width/height
    // unless the capture was shrunk inside the blit.
    int regionWidth = 0;
    int regionHeight = 0;
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

// Captures the whole virtual desktop already downscaled by `scale`, as BGRA only.
// The glass samples at video rates: shrinking inside the blit keeps a single
// small buffer in play instead of copying the entire framebuffer every frame.
bool CaptureScreenScaled(int scale, ScreenFrame* frame, std::wstring* error);

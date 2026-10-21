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
bool CaptureScreen(ScreenFrame* frame, std::wstring* error);

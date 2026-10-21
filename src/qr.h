#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct QrCode {
    std::string payload;
    std::string format;
    // Top-left, top-right, bottom-right, bottom-left, in image pixels.
    double corners[4][2] = {};
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Decodes all readable QR codes from an 8-bit grayscale image.
// Falls back to an inverted copy when nothing is found (dark-mode screenshots).
std::vector<QrCode> DecodeQrCodes(const uint8_t* gray, int width, int height);

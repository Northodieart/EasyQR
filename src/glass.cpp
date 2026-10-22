#include "glass.h"

#include "capture.h"

#include <algorithm>

bool BuildGlassImage(const ScreenFrame& frame, int factor, std::vector<uint32_t>* pixels,
                     int* width, int* height) {
    if (!pixels || !width || !height || factor < 1) return false;
    if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) return false;

    const int smallWidth = std::max(1, (frame.width + factor - 1) / factor);
    const int smallHeight = std::max(1, (frame.height + factor - 1) / factor);
    std::vector<uint32_t> small(static_cast<size_t>(smallWidth) * smallHeight);
    const uint8_t* source = reinterpret_cast<const uint8_t*>(frame.bgra.data());

    for (int y = 0; y < smallHeight; ++y) {
        for (int x = 0; x < smallWidth; ++x) {
            uint64_t red = 0;
            uint64_t green = 0;
            uint64_t blue = 0;
            uint64_t count = 0;
            const int y0 = y * factor;
            const int x0 = x * factor;
            const int y1 = std::min(frame.height, y0 + factor);
            const int x1 = std::min(frame.width, x0 + factor);
            for (int py = y0; py < y1; py += 2) {
                const uint8_t* row = source + static_cast<size_t>(py) * frame.width * 4;
                for (int px = x0; px < x1; px += 2) {
                    const uint8_t* pixel = row + static_cast<size_t>(px) * 4;
                    blue += pixel[0];
                    green += pixel[1];
                    red += pixel[2];
                    ++count;
                }
            }
            if (count == 0) count = 1;
            uint8_t* out =
                reinterpret_cast<uint8_t*>(&small[static_cast<size_t>(y) * smallWidth + x]);
            out[0] = static_cast<uint8_t>(blue / count);
            out[1] = static_cast<uint8_t>(green / count);
            out[2] = static_cast<uint8_t>(red / count);
            out[3] = 255;
        }
    }

    std::vector<uint32_t> blurred(small.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < smallHeight; ++y) {
            for (int x = 0; x < smallWidth; ++x) {
                uint32_t b = 0;
                uint32_t g = 0;
                uint32_t r = 0;
                uint32_t count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int sy = std::clamp(y + dy, 0, smallHeight - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = std::clamp(x + dx, 0, smallWidth - 1);
                        const uint8_t* pixel = reinterpret_cast<const uint8_t*>(
                            &small[static_cast<size_t>(sy) * smallWidth + sx]);
                        b += pixel[0];
                        g += pixel[1];
                        r += pixel[2];
                        ++count;
                    }
                }
                uint8_t* out =
                    reinterpret_cast<uint8_t*>(&blurred[static_cast<size_t>(y) * smallWidth + x]);
                out[0] = static_cast<uint8_t>(b / count);
                out[1] = static_cast<uint8_t>(g / count);
                out[2] = static_cast<uint8_t>(r / count);
                out[3] = 255;
            }
        }
        small.swap(blurred);
    }

    *width = smallWidth;
    *height = smallHeight;
    *pixels = std::move(small);
    return true;
}

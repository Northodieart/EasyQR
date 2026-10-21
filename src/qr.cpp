#include "qr.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

extern "C" {
#include "quirc.h"
}

namespace {

int ClampTo(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

struct QuircDeleter {
    void operator()(quirc* q) const { quirc_destroy(q); }
};

std::vector<QrCode> DecodeOnce(const uint8_t* gray, int width, int height) {
    std::vector<QrCode> codes;
    if (!gray || width < 1 || height < 1) return codes;

    std::unique_ptr<quirc, QuircDeleter> decoder(quirc_new());
    if (!decoder) return codes;

    if (quirc_resize(decoder.get(), width, height) < 0) return codes;

    int image_width = 0;
    int image_height = 0;
    uint8_t* buffer = quirc_begin(decoder.get(), &image_width, &image_height);
    if (!buffer || image_width <= 0 || image_height <= 0) return codes;

    const size_t bytes = static_cast<size_t>(image_width) * static_cast<size_t>(image_height);
    std::memcpy(buffer, gray, bytes);
    quirc_end(decoder.get());

    const int count = quirc_count(decoder.get());
    for (int i = 0; i < count; ++i) {
        quirc_code code{};
        quirc_data data{};
        quirc_extract(decoder.get(), i, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS) continue;
        if (data.payload_len <= 0) continue;

        QrCode result;
        result.payload.assign(reinterpret_cast<const char*>(data.payload),
                              static_cast<size_t>(data.payload_len));

        double min_x = 1e18;
        double min_y = 1e18;
        double max_x = -1e18;
        double max_y = -1e18;
        for (int c = 0; c < 4; ++c) {
            result.corners[c][0] = code.corners[c].x;
            result.corners[c][1] = code.corners[c].y;
            min_x = std::min(min_x, result.corners[c][0]);
            min_y = std::min(min_y, result.corners[c][1]);
            max_x = std::max(max_x, result.corners[c][0]);
            max_y = std::max(max_y, result.corners[c][1]);
        }

        result.x = ClampTo(static_cast<int>(std::floor(min_x)), 0, image_width - 1);
        result.y = ClampTo(static_cast<int>(std::floor(min_y)), 0, image_height - 1);
        const int right = ClampTo(static_cast<int>(std::ceil(max_x)), 0, image_width - 1);
        const int bottom = ClampTo(static_cast<int>(std::ceil(max_y)), 0, image_height - 1);
        result.width = std::max(1, right - result.x + 1);
        result.height = std::max(1, bottom - result.y + 1);

        codes.push_back(std::move(result));
    }

    return codes;
}

bool LooksLikeDuplicate(const QrCode& a, const QrCode& b) {
    if (a.payload != b.payload) return false;
    const double ax = a.x + a.width * 0.5;
    const double ay = a.y + a.height * 0.5;
    const double bx = b.x + b.width * 0.5;
    const double by = b.y + b.height * 0.5;
    const double dx = ax - bx;
    const double dy = ay - by;
    const double limit = std::max({20.0, a.width * 0.5, a.height * 0.5});
    return dx * dx + dy * dy <= limit * limit;
}

}  // namespace

std::vector<QrCode> DecodeQrCodes(const uint8_t* gray, int width, int height) {
    std::vector<QrCode> codes = DecodeOnce(gray, width, height);

    if (codes.empty() && gray && width > 0 && height > 0) {
        const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uint8_t> inverted(size);
        for (size_t i = 0; i < size; ++i) inverted[i] = static_cast<uint8_t>(255 - gray[i]);
        codes = DecodeOnce(inverted.data(), width, height);
    }

    std::vector<QrCode> unique;
    unique.reserve(codes.size());
    for (QrCode& code : codes) {
        bool duplicate = false;
        for (const QrCode& existing : unique) {
            if (LooksLikeDuplicate(existing, code)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) unique.push_back(std::move(code));
    }

    std::sort(unique.begin(), unique.end(), [](const QrCode& a, const QrCode& b) {
        return static_cast<long long>(a.width) * a.height >
               static_cast<long long>(b.width) * b.height;
    });
    return unique;
}

#include "qr.h"

#include "ReadBarcode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

std::vector<QrCode> DecodeWithZXing(const uint8_t* gray, int width, int height) {
    std::vector<QrCode> codes;
    if (!gray || width < 1 || height < 1) return codes;

    try {
        const ZXing::ImageView image(gray, width, height, ZXing::ImageFormat::Lum);
        const ZXing::ReaderOptions options = ZXing::ReaderOptions()
                                                 .setFormats(ZXing::BarcodeFormat::Any)
                                                 .setTryHarder(true)
                                                 .setTryRotate(true)
                                                 .setTryInvert(true)
                                                 .setTryDownscale(true)
                                                 .setMaxNumberOfSymbols(64);
        const ZXing::Results results = ZXing::ReadBarcodes(image, options);

        for (const ZXing::Result& result : results) {
            if (!result.isValid()) continue;
            const std::string payload = result.text();
            if (payload.empty()) continue;

            const ZXing::Position& position = result.position();
            const int minX = std::min({position[0].x, position[1].x, position[2].x, position[3].x});
            const int maxX = std::max({position[0].x, position[1].x, position[2].x, position[3].x});
            int minY = std::min({position[0].y, position[1].y, position[2].y, position[3].y});
            int maxY = std::max({position[0].y, position[1].y, position[2].y, position[3].y});
            if (maxX - minX < 4) continue;
            if (maxX < 0 || maxY < 0 || minX >= width || minY >= height) continue;

            // 1D flags have a line-like position (zero height); give them a small
            // virtual box so the overlay can place and highlight a result button.
            if (maxY - minY < 4) {
                const int centerY = (minY + maxY) / 2;
                minY = centerY - 8;
                maxY = centerY + 8;
            }

            QrCode code;
            code.payload = payload;
            code.format = ZXing::ToString(result.format());
            for (int i = 0; i < 4; ++i) {
                code.corners[i][0] = position[i].x;
                code.corners[i][1] = position[i].y;
            }
            code.x = ClampTo(minX, 0, width - 1);
            code.y = ClampTo(minY, 0, height - 1);
            code.width = std::max(1, ClampTo(maxX, 0, width - 1) - code.x + 1);
            code.height = std::max(1, ClampTo(maxY, 0, height - 1) - code.y + 1);
            codes.push_back(std::move(code));
        }
    } catch (const std::exception&) {
        // A decode failure must never take down the UI; the quirc fallback still runs.
    }
    return codes;
}

std::vector<QrCode> DecodeWithQuirc(const uint8_t* gray, int width, int height) {
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
        result.format = "QRCode";

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

void AppendUnique(std::vector<QrCode>* destination, std::vector<QrCode>&& source) {
    for (QrCode& code : source) {
        bool duplicate = false;
        for (const QrCode& existing : *destination) {
            if (LooksLikeDuplicate(existing, code)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) destination->push_back(std::move(code));
    }
}

}  // namespace

std::vector<QrCode> DecodeQrCodes(const uint8_t* gray, int width, int height) {
    if (!gray || width < 1 || height < 1) return {};

    // ZXing handles stylized, small and multiple codes in one pass. quirc remains
    // as a second opinion for the rare image ZXing's detector rejects.
    std::vector<QrCode> codes = DecodeWithZXing(gray, width, height);
    AppendUnique(&codes, DecodeWithQuirc(gray, width, height));

    if (codes.empty()) {
        const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uint8_t> inverted(size);
        for (size_t i = 0; i < size; ++i) inverted[i] = static_cast<uint8_t>(255 - gray[i]);
        AppendUnique(&codes, DecodeWithQuirc(inverted.data(), width, height));
    }

    std::sort(codes.begin(), codes.end(), [](const QrCode& a, const QrCode& b) {
        return static_cast<long long>(a.width) * a.height >
               static_cast<long long>(b.width) * b.height;
    });
    return codes;
}

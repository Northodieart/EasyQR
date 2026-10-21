#include "qr.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool ReadToken(std::istream& in, std::string* token) {
    *token = "";
    char c = 0;
    while (in.get(c)) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '#') {
            std::string ignored;
            std::getline(in, ignored);
            continue;
        }
        break;
    }
    if (!in) return false;
    token->push_back(c);
    while (in.get(c)) {
        if (std::isspace(static_cast<unsigned char>(c))) break;
        token->push_back(c);
    }
    return in.good() || in.eof();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: qr_probe <image.pgm>\n");
        return 2;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }

    std::string magic;
    std::string widthText;
    std::string heightText;
    std::string maxText;
    if (!ReadToken(file, &magic) || magic != "P5" || !ReadToken(file, &widthText) ||
        !ReadToken(file, &heightText) || !ReadToken(file, &maxText)) {
        std::fprintf(stderr, "invalid PGM header\n");
        return 2;
    }

    const int width = std::atoi(widthText.c_str());
    const int height = std::atoi(heightText.c_str());
    if (width <= 0 || height <= 0 || maxText != "255") {
        std::fprintf(stderr, "unsupported PGM\n");
        return 2;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height);
    file.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!file) {
        std::fprintf(stderr, "truncated PGM\n");
        return 2;
    }

    const std::vector<QrCode> codes = DecodeQrCodes(pixels.data(), width, height);
    std::vector<std::string> payloads;
    payloads.reserve(codes.size());
    for (const QrCode& code : codes) payloads.push_back(code.payload);
    std::sort(payloads.begin(), payloads.end());
    for (const std::string& payload : payloads) std::printf("%s\n", payload.c_str());
    return 0;
}

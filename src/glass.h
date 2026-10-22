#pragma once

#include <cstdint>
#include <vector>

struct ScreenFrame;

bool BuildGlassImage(const ScreenFrame& frame, int factor, std::vector<uint32_t>* pixels,
                     int* width, int* height);

#pragma once
#include <cstdint>

namespace streambrew {

enum class PixelFormat {
    RGB,
    BGR,
    GRAY8
};

struct Frame {
    const std::uint8_t *data;
    int width;
    int height;
    int stride;
    PixelFormat format;
};

} // namespace streambrew

#pragma once
#include <cstdint>

namespace streambrew {
struct Frame {
    const std::uint8_t *data;
    int width;
    int height;
    int stride;
};
} // namespace streambrew
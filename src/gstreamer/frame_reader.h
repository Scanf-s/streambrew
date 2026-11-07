#pragma once

#include <functional>
#include <gst/gst.h>

#include "frame/frame.h"

namespace streambrew::gst {

// Maps the sample only for the duration of the callback, including exception unwinding.
void deliver_frame(GstSample *sample, PixelFormat format,
                   const std::function<void(const Frame &)> &on_frame);

} // namespace streambrew::gst

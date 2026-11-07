#pragma once

#include <gst/gst.h>

namespace streambrew::gst {

enum class BusStatus { Running, Eos };

// Waits for an error/EOS, then drains matching messages already queued.
// Errors throw even if an EOS message was encountered first.
BusStatus poll_bus(GstBus *bus, GstClockTime timeout = 0);

} // namespace streambrew::gst

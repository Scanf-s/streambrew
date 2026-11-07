#pragma once

#include <string>

#include "gstreamer/gst_ptr.h"

namespace streambrew::gst {

// APPLICATION messages wake an observer after a control or frame notification.
MessagePtr wait_for_bus_message(GstBus *bus, GstClockTime timeout);
void wake_bus(GstBus *bus) noexcept;
std::string describe_bus_message(GstMessage *message);

} // namespace streambrew::gst

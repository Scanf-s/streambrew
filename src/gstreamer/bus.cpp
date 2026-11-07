#include "gstreamer/bus.h"

namespace streambrew::gst {

MessagePtr wait_for_bus_message(GstBus *bus, GstClockTime timeout) {
    const auto types = static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING |
                                                   GST_MESSAGE_EOS | GST_MESSAGE_APPLICATION);
    return MessagePtr(gst_bus_timed_pop_filtered(bus, timeout, types));
}

void wake_bus(GstBus *bus) noexcept {
    gst_bus_post(bus, gst_message_new_application(
                          nullptr, gst_structure_new_empty("streambrew-observer-wakeup")));
}

std::string describe_bus_message(GstMessage *message) {
    GError *raw_error = nullptr;
    gchar *raw_debug = nullptr;
    const bool warning = GST_MESSAGE_TYPE(message) == GST_MESSAGE_WARNING;
    if (warning) {
        gst_message_parse_warning(message, &raw_error, &raw_debug);
    } else {
        gst_message_parse_error(message, &raw_error, &raw_debug);
    }
    ErrorPtr error(raw_error);
    CharPtr debug(raw_debug);
    const char *source =
        GST_MESSAGE_SRC(message) ? GST_OBJECT_NAME(GST_MESSAGE_SRC(message)) : "unknown";
    std::string text =
        std::string(warning ? "Warning from " : "Error from ") + source + ": " + error->message;
    if (debug) {
        text += "\nDebug details: ";
        text += debug.get();
    }
    return text;
}

} // namespace streambrew::gst

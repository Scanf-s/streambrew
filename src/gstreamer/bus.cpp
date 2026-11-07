#include "gstreamer/bus.h"
#include "gstreamer/gst_ptr.h"

#include <stdexcept>
#include <string>

namespace streambrew::gst {

BusStatus poll_bus(GstBus *bus, GstClockTime timeout) {
    BusStatus status = BusStatus::Running;
    while (true) {
        MessagePtr msg(gst_bus_timed_pop_filtered(
            bus, timeout, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)));
        if (msg == nullptr) {
            return status;
        }
        timeout = 0;
        if (GST_MESSAGE_TYPE(msg.get()) == GST_MESSAGE_EOS) {
            status = BusStatus::Eos;
            continue;
        }

        GError *raw_error = nullptr;
        gchar *raw_debug = nullptr;
        gst_message_parse_error(msg.get(), &raw_error, &raw_debug);
        ErrorPtr error(raw_error);
        CharPtr debug_info(raw_debug);
        std::string message =
            std::string("Error from ") + GST_OBJECT_NAME(msg->src) + ": " + error->message;
        if (debug_info != nullptr) {
            message += "\nDebug details: ";
            message += debug_info.get();
        }
        throw std::runtime_error(message);
    }
}

} // namespace streambrew::gst

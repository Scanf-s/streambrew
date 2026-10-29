#include "pipeline/frame_pipeline.h"
#include "gstreamer/gst_ptr.h"
#include "frame/frame.h"

#include <stdexcept>
#include <gst/video/video.h>
#include <gst/app/gstappsink.h>

namespace streambrew {
// Define FramePipeline constructor
FramePipeline::FramePipeline(const std::string &uri, const std::string &format, int fps)
    : pipeline_(gst_element_factory_make("playbin", nullptr)) {
    if (pipeline_ == nullptr) {
        throw std::runtime_error("Could not create the GStreamer playbin element.");
    }
    g_object_set(pipeline_.get(), "uri", uri.c_str(), nullptr);

    // Normalize decoded video to a fixed frame rate.
    const std::string description =
        "videorate ! "
        "videoconvert ! "
        "video/x-raw,format=" + format + ",framerate=" + std::to_string(fps) + "/1 ! "
        "appsink name=frames sync=true";
    GError *raw_error = nullptr;
    gst::ObjectPtr<GstElement> sink_bin(gst_parse_bin_from_description(
        description.c_str(),
        TRUE, // Automatically create a ghost pad for the unconnected input.
        &raw_error
    ));
    gst::ErrorPtr error(raw_error);
    
    if (error != nullptr || sink_bin == nullptr) {
        std::string message = error != nullptr ? error->message : "unknown error";
        throw std::runtime_error("Cannot create video sink: " + message);
    }

    // playbin takes ownership of sink_bin's floating reference.
    // So sink_bin must give up its ownership by calling .release()
    GstElement *sink_bin_view = sink_bin.release();
    g_object_set(pipeline_.get(), "video-sink", sink_bin_view, nullptr);
    appsink_.reset(gst_bin_get_by_name(GST_BIN(sink_bin_view), "frames"));
    if (appsink_ == nullptr) {
        throw std::runtime_error("Could not find the appsink element.");
    }

    bus_.reset(gst_element_get_bus(pipeline_.get()));
}

FramePipeline::~FramePipeline() {
    // playbin must reach the NULL state before its last reference is released.
    gst_element_set_state(pipeline_.get(), GST_STATE_NULL);
}

// run pre-defined GStreamer video processing pipeline
bool FramePipeline::run(const FrameCallback &on_frame) {
    if (
        gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING) == 
        GST_STATE_CHANGE_FAILURE
    ) {
        // The element that failed usually posts the reason on the bus.
        poll_bus();
        return false;
    }

    while (true) {
        BusStatus status = poll_bus();
        if (status == BusStatus::Error) {
            return false;
        }
        if (status == BusStatus::Eos) {
            return true;
        }

        // Get a processed frame from the appsink (wait up to 100ms)
        gst::SamplePtr sample(
            gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_.get()),
            100 * GST_MSECOND)
        );
        if (sample == nullptr) {
            if (gst_app_sink_is_eos(GST_APP_SINK(appsink_.get()))) {
                return true;
            }
            continue;
        }
        
        GstVideoInfo video_info;
        if (!gst_video_info_from_caps(&video_info, gst_sample_get_caps(sample.get()))) {
            continue;
        }

        GstVideoFrame video_frame;
        if (!gst_video_frame_map(&video_frame, &video_info, gst_sample_get_buffer(sample.get()), GST_MAP_READ)) {
            continue;
        }

        streambrew::Frame frame;
        frame.data = static_cast<const std::uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&video_frame, 0));
        frame.width = GST_VIDEO_FRAME_WIDTH(&video_frame);
        frame.height = GST_VIDEO_FRAME_HEIGHT(&video_frame);
        frame.stride = GST_VIDEO_FRAME_PLANE_STRIDE(&video_frame, 0);

        // call user-defined callback function for their own frame processing logic
        on_frame(frame);
        gst_video_frame_unmap(&video_frame);
    }
}

// poll_bus checks the event message from GStreamer sent 
FramePipeline::BusStatus FramePipeline::poll_bus() {
    gst::MessagePtr msg(
        gst_bus_pop_filtered(
            bus_.get(),
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)
        )
    );
    if (msg == nullptr) {
        return BusStatus::Running;
    }
    if (GST_MESSAGE_TYPE(msg.get()) == GST_MESSAGE_EOS) {
        return BusStatus::Eos;
    }

    GError* raw_error = nullptr;
    gchar* raw_debug = nullptr;
    gst_message_parse_error(msg.get(), &raw_error, &raw_debug);

    // Change error, debug message ptr ownership (for safe manage management)
    gst::ErrorPtr error(raw_error);
    gst::CharPtr debug_info(raw_debug);
    g_printerr("Error from %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
    if (debug_info != nullptr) {
        g_printerr("Debug details: %s\n", debug_info.get());
    }

    return BusStatus::Error;
}
} // namespace streambrew

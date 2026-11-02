#include "pipeline/frame_pipeline.h"
#include "gstreamer/gst_ptr.h"
#include "frame/frame.h"

#include <stdexcept>
#include <gst/video/video.h>
#include <gst/app/gstappsink.h>

namespace streambrew {

namespace {
GstElement *create_playbin() {
    gst_init(nullptr, nullptr);
    return gst_element_factory_make("playbin", nullptr);
}

const char *to_gst_format(PixelFormat format) {
    switch (format) {
    case PixelFormat::RGB:
        return "RGB";
    case PixelFormat::BGR:
        return "BGR";
    case PixelFormat::GRAY8:
        return "GRAY8";
    }
    throw std::invalid_argument("invalid pixel format");
}

std::string to_uri(const std::string &source) {
    if (gst_uri_is_valid(source.c_str())) {
        return source;
    }
    GError *raw_error = nullptr;
    gst::CharPtr uri(gst_filename_to_uri(source.c_str(), &raw_error));
    gst::ErrorPtr error(raw_error);
    if (uri == nullptr) {
        throw std::runtime_error("cannot convert " + source + " to a URI: " + error->message);
    }
    return uri.get();
}
} // namespace

// Define FramePipeline constructor
FramePipeline::FramePipeline(
    const std::string &source,
    PixelFormat format, 
    int fps) 
    : pixel_format_(format), pipeline_(create_playbin()) {

    if (pipeline_ == nullptr) {
        throw std::runtime_error("Could not create the GStreamer playbin element.");
    }
    if (fps <= 0) {
        throw std::invalid_argument("fps must be greater than 0");
    }
    const std::string uri = to_uri(source);
    g_object_set(pipeline_.get(), "uri", uri.c_str(), nullptr);

    // Normalize decoded video to a fixed frame rate.
    const std::string description =
        "videorate ! "
        "videoconvert ! "
        "video/x-raw,format=" + std::string(to_gst_format(format)) + ",framerate=" + std::to_string(fps) + "/1 ! "
        "appsink name=frames sync=true";
    
    // create new sink_bin to attach whole description pipeline elements 
    // into the video-sink property of playbin
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
void FramePipeline::run(const FrameCallback &on_frame) {
    if (
        gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING) == 
        GST_STATE_CHANGE_FAILURE
    ) {
        // The element that failed usually posts the reason on the bus.
        poll_bus();
        throw std::runtime_error("Could not start the pipeline.");
    }

    while (true) {
        BusStatus status = poll_bus();
        if (status == BusStatus::Eos) {
            break;
        }

        // Get a processed frame from the appsink (wait up to 100ms)
        gst::SamplePtr sample(
            gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_.get()),
            100 * GST_MSECOND)
        );
        if (sample == nullptr) {
            if (gst_app_sink_is_eos(GST_APP_SINK(appsink_.get()))) {
                break;
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
        frame.format = pixel_format_;

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
    std::string message = std::string("Error from ") + GST_OBJECT_NAME(msg->src) + ": " + error->message;
    if (debug_info != nullptr) {
        message += "\nDebug details: ";
        message += debug_info.get();
    }
    throw std::runtime_error(message);
}
} // namespace streambrew

#include "gstreamer/pipeline_builder.h"

#include <stdexcept>

namespace streambrew::gst {
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

void validate_options(const PipelineOptions &options) {
    if (options.timing != DeliveryTiming::AsFastAsPossible &&
        options.timing != DeliveryTiming::Clocked) {
        throw std::invalid_argument("invalid delivery timing");
    }

    if (options.overflow != OverflowPolicy::BlockProducer &&
        options.overflow != OverflowPolicy::DropOldest) {
        throw std::invalid_argument("invalid overflow policy");
    }

    if (options.max_pending_frames == 0 || options.max_pending_frames > G_MAXUINT) {
        throw std::invalid_argument("max_pending_frames must be in [1, G_MAXUINT]");
    }
}

std::string to_uri(const std::string &source) {
    if (gst_uri_is_valid(source.c_str())) {
        return source;
    }
    GError *raw_error = nullptr;
    CharPtr uri(gst_filename_to_uri(source.c_str(), &raw_error));
    ErrorPtr error(raw_error);
    if (uri == nullptr) {
        std::string message = error != nullptr ? error->message : "unknown error";
        throw std::runtime_error("cannot convert " + source + " to a URI: " + message);
    }
    return uri.get();
}
} // namespace

PipelineElements build_frame_pipeline(const std::string &source, PixelFormat format,
                                      const PipelineOptions &options, int fps) {
    PipelineElements elements;
    elements.pipeline.reset(create_playbin());
    if (elements.pipeline == nullptr) {
        throw std::runtime_error("Could not create the GStreamer playbin element.");
    }
    if (fps <= 0) {
        throw std::invalid_argument("fps must be greater than 0");
    }

    validate_options(options);

    const std::string uri = to_uri(source);

    g_object_set(elements.pipeline.get(), "uri", uri.c_str(), nullptr);

    if (options.timing == DeliveryTiming::AsFastAsPossible) {
        // Disable audio so an audio sink cannot pace file processing
        constexpr guint play_flag_audio = 1u << 1;
        guint flags = 0;
        g_object_get(elements.pipeline.get(), "flags", &flags, nullptr);
        g_object_set(elements.pipeline.get(), "flags", flags & ~play_flag_audio, nullptr);
    }

    // Normalize decoded video to a fixed frame rate.
    const std::string description = "videorate ! "
                                    "videoconvert ! "
                                    "video/x-raw,format=" +
                                    std::string(to_gst_format(format)) +
                                    ",framerate=" + std::to_string(fps) +
                                    "/1 ! "
                                    "appsink name=frames";

    // create new sink_bin to attach whole description pipeline elements
    // into the video-sink property of playbin
    GError *raw_error = nullptr;
    ObjectPtr<GstElement> sink_bin(gst_parse_bin_from_description(
        description.c_str(),
        TRUE, // Automatically create a ghost pad for the unconnected input.
        &raw_error));
    ErrorPtr error(raw_error);
    if (error != nullptr || sink_bin == nullptr) {
        std::string message = error != nullptr ? error->message : "unknown error";
        throw std::runtime_error("Cannot create video sink: " + message);
    }

    // playbin takes ownership of sink_bin's floating reference.
    // So sink_bin must give up its ownership by calling .release()
    GstElement *sink_bin_view = sink_bin.release();
    g_object_set(elements.pipeline.get(), "video-sink", sink_bin_view, nullptr);
    elements.appsink.reset(gst_bin_get_by_name(GST_BIN(sink_bin_view), "frames"));
    if (elements.appsink == nullptr) {
        throw std::runtime_error("Could not find the appsink element.");
    }

    g_object_set(elements.appsink.get(), "sync",
                 static_cast<gboolean>(options.timing == DeliveryTiming::Clocked), "max-buffers",
                 static_cast<guint>(options.max_pending_frames), "drop",
                 static_cast<gboolean>(options.overflow == OverflowPolicy::DropOldest), nullptr);

    elements.bus.reset(gst_element_get_bus(elements.pipeline.get()));
    return elements;
}

} // namespace streambrew::gst

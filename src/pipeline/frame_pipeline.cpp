#include "pipeline/frame_pipeline.h"
#include "gstreamer/bus.h"
#include "gstreamer/frame_reader.h"
#include "gstreamer/pipeline_builder.h"

#include <gst/app/gstappsink.h>
#include <stdexcept>
#include <utility>

namespace streambrew {

FramePipeline::FramePipeline(const std::string &source, PixelFormat format, int fps)
    : FramePipeline(source, format, PipelineOptions{}, fps) {}

FramePipeline::FramePipeline(const std::string &source, PixelFormat format,
                             const PipelineOptions &options, int fps)
    : pixel_format_(format) {
    auto elements = gst::build_frame_pipeline(source, format, options, fps);
    pipeline_ = std::move(elements.pipeline);
    appsink_ = std::move(elements.appsink);
    bus_ = std::move(elements.bus);
}

FramePipeline::~FramePipeline() { gst_element_set_state(pipeline_.get(), GST_STATE_NULL); }

void FramePipeline::run(const FrameCallback &on_frame) {
    if (gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        // The element that failed usually posts the reason on the bus.
        gst::poll_bus(bus_.get());
        throw std::runtime_error("Could not start the pipeline.");
    }

    while (true) {
        gst::BusStatus status = gst::poll_bus(bus_.get());
        if (status == gst::BusStatus::Eos) {
            break;
        }

        // Get a processed frame from the appsink (wait up to 100ms)
        gst::SamplePtr sample(
            gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_.get()), 100 * GST_MSECOND));
        if (sample == nullptr) {
            if (gst_app_sink_is_eos(GST_APP_SINK(appsink_.get()))) {
                break;
            }
            continue;
        }

        gst::deliver_frame(sample.get(), pixel_format_, on_frame);
    }
}

} // namespace streambrew

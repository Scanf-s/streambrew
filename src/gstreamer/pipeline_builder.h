#pragma once

#include <string>

#include "frame/frame.h"
#include "gstreamer/gst_ptr.h"
#include "pipeline/pipeline_options.h"

namespace streambrew::gst {

struct PipelineElements {
    ObjectPtr<GstElement> pipeline;
    ObjectPtr<GstElement> appsink;
    ObjectPtr<GstBus> bus;
};

PipelineElements build_frame_pipeline(const std::string &source, PixelFormat format,
                                      const PipelineOptions &options);

} // namespace streambrew::gst

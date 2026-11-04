#pragma once

#include <memory>

#include <gst/gst.h>
#include <gst/video/video.h>

namespace streambrew::gst {

struct ObjectUnref {
    void operator()(gpointer object) const { gst_object_unref(object); }
};

struct MessageUnref {
    void operator()(GstMessage *message) const { gst_message_unref(message); }
};

struct SampleUnref {
    void operator()(GstSample *sample) const { gst_sample_unref(sample); }
};

struct Free {
    void operator()(gpointer memory) const { g_free(memory); }
};

struct ErrorFree {
    void operator()(GError *error) const { g_error_free(error); }
};

struct VideoFrameUnmap {
    void operator()(GstVideoFrame *frame) const { gst_video_frame_unmap(frame); }
};

template <typename T> using ObjectPtr = std::unique_ptr<T, ObjectUnref>;
using MessagePtr = std::unique_ptr<GstMessage, MessageUnref>;
using SamplePtr = std::unique_ptr<GstSample, SampleUnref>;
using CharPtr = std::unique_ptr<gchar, Free>;
using ErrorPtr = std::unique_ptr<GError, ErrorFree>;

// Unlike the other pointers, this does not own the GstVideoFrame.
// It only unmaps it, so the GstVideoFrame must stay alive until this pointer is destroyed.
using VideoFramePtr = std::unique_ptr<GstVideoFrame, VideoFrameUnmap>;

} // namespace streambrew::gst

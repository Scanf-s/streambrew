#pragma once

#include <memory>

#include <gst/gst.h>

namespace streambrew::gst {

struct ObjectUnref {
    void operator()(gpointer object) const { gst_object_unref(object); }
};
template <typename T>
using ObjectPtr = std::unique_ptr<T, ObjectUnref>;

struct MessageUnref {
    void operator()(GstMessage *message) const { gst_message_unref(message); }
};
using MessagePtr = std::unique_ptr<GstMessage, MessageUnref>;

struct SampleUnref {
    void operator()(GstSample *sample) const { gst_sample_unref(sample); }
};
using SamplePtr = std::unique_ptr<GstSample, SampleUnref>;

struct Free {
    void operator()(gpointer memory) const { g_free(memory); }
};
using CharPtr = std::unique_ptr<gchar, Free>;

struct ErrorFree {
    void operator()(GError *error) const { g_error_free(error); }
};
using ErrorPtr = std::unique_ptr<GError, ErrorFree>;

} // namespace streambrew::gst

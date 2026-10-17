#pragma once

#include <memory>

#include <gst/gst.h>

namespace streambrew::gst {

struct GstObjectUnref {
    void operator()(gpointer object) const { gst_object_unref(object); }
};
template <typename T>
using GstObjectPtr = std::unique_ptr<T, GstObjectUnref>;

struct GstMessageUnref {
    void operator()(GstMessage *message) const { gst_message_unref(message); }
};
using GstMessagePtr = std::unique_ptr<GstMessage, GstMessageUnref>;

struct GstSampleUnref {
    void operator()(GstSample *sample) const { gst_sample_unref(sample); }
};
using GstSamplePtr = std::unique_ptr<GstSample, GstSampleUnref>;

struct GFree {
    void operator()(gpointer memory) const { g_free(memory); }
};
using GCharPtr = std::unique_ptr<gchar, GFree>;

struct GErrorClear {
    void operator()(GError *error) const { g_error_free(error); }
};
using GErrorPtr = std::unique_ptr<GError, GErrorClear>;

}
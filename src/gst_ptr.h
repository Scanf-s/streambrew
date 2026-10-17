#pragma once

#include <memory>

#include <gst/gst.h>

// Deleters that std::unique_ptr calls when the pointer goes out of scope.
struct GstObjectUnref {
    void operator()(gpointer object) const { gst_object_unref(object); }
};

struct GstMessageUnref {
    void operator()(GstMessage *message) const { gst_message_unref(message); }
};

struct GstSampleUnref {
    void operator()(GstSample *sample) const { gst_sample_unref(sample); }
};

struct GFree {
    void operator()(gpointer memory) const { g_free(memory); }
};


template <typename T>
using GstObjectPtr = std::unique_ptr<T, GstObjectUnref>;
using GstMessagePtr = std::unique_ptr<GstMessage, GstMessageUnref>;
using GstSamplePtr = std::unique_ptr<GstSample, GstSampleUnref>;
using GCharPtr = std::unique_ptr<gchar, GFree>;

#include <gst/gst.h>
#include <iostream>
#include <chrono>
#include <exception>

#include "pipeline/frame_pipeline.h"
#include "gstreamer/gst_ptr.h"
#include "frame/frame.h"

using Clock = std::chrono::steady_clock;

int main(int argc, char *argv[]) {
    // Initialize GStreamer.
    gst_init(&argc, &argv);

    const char *filename = argc > 1 ? argv[1] : "sample.mp4";
    GError *error = nullptr;
    streambrew::gst::CharPtr uri(gst_filename_to_uri(filename, &error));
    if (uri == nullptr) {
        g_printerr("Cannot convert %s to a URI: %s\n", filename, error->message);
        g_clear_error(&error);
        return 1;
    }

    // Setup clock for measuring FPS
    auto window_start = Clock::now();
    unsigned int frame_count = 0;

    // Define a callback function for custom frame processing logic
    auto on_frame = [&](const streambrew::Frame &frame) {
        ++frame_count;
        // DO SOMETHING
        auto now = Clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - window_start).count();
        if (elapsed_sec >= 1.0) {
            std::cout << "frame info" << '\n';
            std::cout << frame.width << "x" << frame.height << " stride=" << frame.stride << '\n';
            std::cout << "Received FPS: " << frame_count / elapsed_sec << '\n';
            frame_count = 0;
            window_start = now;
        }
    };

    try {
        streambrew::FramePipeline pipeline(uri.get(), "RGB");
        if (!pipeline.run(on_frame)) {
            return 1;
        }
    } catch (const std::exception &e) {
        g_printerr("%s\n", e.what());
        return 1;
    }

    g_print("End of stream.\n");
    return 0;
}

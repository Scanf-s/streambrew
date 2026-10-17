#include <gst/gst.h>
#include <iostream>
#include <chrono>
#include <gst/app/gstappsink.h>

int main(int argc, char *argv[]) {
    // Initialize GStreamer.
    gst_init(&argc, &argv);

    const char *filename = argc > 1 ? argv[1] : "sample.mp4";
    GError *error = nullptr;
    gchar *uri = gst_filename_to_uri(filename, &error);
    if (uri == nullptr) {
        g_printerr("Cannot convert %s to a URI: %s\n", filename, error->message);
        g_clear_error(&error);
        return 1;
    }

    // Build pipeline elements
    // Playbin element
    GstElement *playbin = gst_element_factory_make("playbin", nullptr);
    if (playbin == nullptr) {
        g_printerr("Could not create the GStreamer playbin element.\n");
        g_free(uri);
        return 1;
    }
    g_object_set(playbin, "uri", uri, nullptr);
    g_free(uri);

    // Normalize decoded video to a 30 FPS timeline.
    GstElement* sink_bin = gst_parse_bin_from_description(
        "videorate ! "
        "video/x-raw,framerate=30/1 ! "
        "appsink name=frames sync=true",
        TRUE, // Automatically create a ghost pad for the unconnected input.
        &error
    );
    if (error != nullptr || sink_bin == nullptr) {
        g_printerr("Cannot create video sink: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        if (sink_bin != nullptr) {
            gst_object_unref(sink_bin);
        }
        gst_object_unref(playbin);
        return 1;
    }

    // playbin has responsibility to clean up the sink_bin
    g_object_set(playbin, "video-sink", sink_bin, nullptr);

    // get appsink element from sink_bin
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(sink_bin), "frames");
    if (appsink == nullptr) {
        g_printerr("Could not create the GStreamer appsink element\n");
        gst_object_unref(playbin);
        return 1;
    }

    // Start playing.
    gst_element_set_state(playbin, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(playbin);

    // Setup clock for measuring FPS
    using Clock = std::chrono::steady_clock;
    auto window_start = Clock::now();
    unsigned int frame_count = 0;

    // Get processed frame from the appsink consecutively before getting an error or EOS.
    bool running = true;
    while(running) {
        // Check if any error was occured
        GstMessage *msg = gst_bus_pop_filtered(
            bus,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)
        );
        if (msg != nullptr) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError *error = nullptr;
                gchar *debug_info = nullptr;
                gst_message_parse_error(msg, &error, &debug_info);
                g_printerr("Error from %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
                if (debug_info != nullptr) {
                    g_printerr("Debug details: %s\n", debug_info);
                }
                g_clear_error(&error);
                g_free(debug_info);
                running = false;
            } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
                g_print("End of stream.\n");
            }
            gst_message_unref(msg);
        }

        if (!running) {
            break;
        }

        // Get a processed frame from the playback (wait up to 100ms)
        GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 100 * GST_MSECOND);
        if (sample == nullptr) {
            // Break the loop on EOS.
            if (gst_app_sink_is_eos(GST_APP_SINK(appsink))) {
                break;
            }
            continue;
        }
        
        GstBuffer* frame = gst_sample_get_buffer(sample);
        if (frame != nullptr) {
            ++frame_count;
            // DO SOMETHING
            auto now = Clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - window_start).count();
            if (elapsed_sec >= 1.0) {
                std::cout << "Received FPS: " << frame_count / elapsed_sec << '\n';
                frame_count = 0;
                window_start = now;
            }
        }

        gst_sample_unref(sample);
    }

    // Clean up.
    gst_element_set_state(playbin, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(playbin);
    gst_object_unref(appsink);
    return 0;
}

#include <iostream>
#include <chrono>
#include <exception>

#include "pipeline/frame_pipeline.h"
#include "frame/frame.h"

using Clock = std::chrono::steady_clock;

int main(int argc, char *argv[]) {
    const char *source = argc > 1 ? argv[1] : "sample.mp4";

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

    // Request video pipeline
    try {
        streambrew::FramePipeline pipeline(source, "RGB");
        pipeline.run(on_frame);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    std::cout << "End of stream.\n";
    return 0;
}

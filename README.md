# Streambrew

Video processing experiments

## Build and run

Install the GStreamer development package, `pkg-config`, CMake, and a C++ compiler. From the project root, run:

```sh
cmake -S . -B build
cmake --build build
./build/main sample.mp4
```

Pass another file path to play a different video. VS Code reads the include paths from CMake's `build/compile_commands.json`.

## Development commands

```sh
make build                   # Configure and build
make run SOURCE=sample.mp4   # Build and process a video
make test                    # Build and run tests
make format                  # Apply formatting to C++ sources and headers
make format-check            # Check formatting without changing files
make lint                    # Run static analysis; warnings cause failure
make check                   # Run format-check, lint, and tests
make clean                   # Remove build outputs, keeping CMake configuration
```

Override the build directory, build type, or parallelism as needed

```sh
make test BUILD_DIR=build-debug BUILD_TYPE=Debug JOBS=4
```

Tool paths can also be overridden, for example `make lint CLANG_TIDY=clang-tidy-18`.

## Frame delivery API

```cpp
#include "pipeline/frame_pipeline.h"

streambrew::PipelineOptions options;
options.timing = streambrew::DeliveryTiming::AsFastAsPossible;
options.overflow = streambrew::OverflowPolicy::BlockProducer;
options.max_pending_frames = 4;

streambrew::FramePipeline pipeline("sample.mp4", streambrew::PixelFormat::RGB, options);
pipeline.run([](const streambrew::Frame &frame) {
    // Process frame.data here, using frame.stride to advance between rows.
});
```

| Option | Behavior |
| --- | --- |
| `AsFastAsPossible` | Processes delivered frames as fast as possible. Disables audio playback. Live sources (e.g. RTSP) still produce frames at their own rate |
| `Clocked` (default) | Wait for video timestamps on the pipeline clock |
| `BlockProducer` (default) | Wait for queue space and preserve queued frames in order |
| `DropOldest` | Discard the oldest pending frame when the queue is full. Only preserve newer frames |

`max_pending_frames` defaults to **1** and must be in `[1, G_MAXUINT]`.
It limits the appsink queue, excluding the frame in the callback and upstream buffers.

## Tests

The integration test generates its own AVI fixtures, including one with audio.
It requires the GStreamer Base and Good runtime plugins (`videotestsrc`,
`audiotestsrc`, `avimux`, and `avidemux`).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

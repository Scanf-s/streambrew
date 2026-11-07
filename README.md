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

## Run control

```cpp
using namespace std::chrono_literals;

streambrew::FramePipeline pipeline("sample.mp4", streambrew::PixelFormat::RGB);
streambrew::RunOptions options;
options.first_frame_timeout = 5s;
options.frame_wait_timeout = 2s;

int processed = 0;
const auto result = pipeline.run([&](const streambrew::Frame &frame) {
    // Process frame here. Its data is valid only during this callback.
    if (++processed == 100) {
        pipeline.request_stop();
    }
}, options);
```

Each instance runs once. `state()` reports `Ready`, `Running`, or `Finished`.
`run()` returns `EndOfStream` after draining frames or `Stopped` after a stop
request. Concurrent, recursive, and repeated runs throw `std::logic_error`.

`request_stop()` and `state()` can be called from another thread. Stop requests
are also allowed before `run()` and inside callbacks. The current callback
finishes, pending frames are discarded, and the pipeline reaches `NULL` before
`run()` returns. Exceptions also trigger teardown before propagating.

Timeouts are disabled by default; configured values must be positive.
`first_frame_timeout` starts before playback begins. `frame_wait_timeout` starts
after each callback and frame cleanup, excluding consumer processing time.
`PipelineTimeout::stage()` distinguishes `FirstFrame` from `NextFrame`.
Invalid options or an empty callback do not consume the instance's run.

Cancellation and timeouts cannot interrupt callbacks or blocking plugin calls.
The loop polls at most every 100 ms while waiting; teardown may take additional
time. Wait for `run()` to finish and join its thread before destroying the object.

## Code layout

- `pipeline/frame_pipeline.cpp`: construction, execution state, and teardown.
- `pipeline/frame_pipeline_run.cpp`: frame waiting, stop checks, and timeout decisions.
- `gstreamer/`: pipeline construction, bus events, frame mapping, and native resource ownership.
- `utils/`: generic scope-exit cleanup and elapsed-time budgets, independent of GStreamer.

## Tests

The integration test generates its own AVI fixtures, including one with audio.
It requires the GStreamer Base and Good runtime plugins (`videotestsrc`,
`audiotestsrc`, `avimux`, and `avidemux`).

Lifecycle tests use an in-process `appsrc` to check silent/stalled inputs, stop
requests, timeout stages, exception cleanup, and concurrent/repeated runs.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

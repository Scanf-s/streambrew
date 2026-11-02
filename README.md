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

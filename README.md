# WCLAP test plug-ins

Small source-built CLAP/WCLAP fixtures for testing browser host behavior.

## Fixtures

### `gui-lifecycle`

A stereo pass-through with an embedded WebView. Its `clap.gui` implementation enforces the embedded lifecycle order `create` → `get_size` → `set_parent` → `show`, and accepts its automatic ready-message round trip only while visible.

### `process-lifecycle`

A GUI-free utility with two mono outputs and one note input. It makes every CLAP process status, sleep wake path, `steady_time`, and finite/infinite tail behavior observable. See [`process-lifecycle/README.md`](process-lifecycle/README.md) for the MIDI protocol and exact browser-host checks.

## Download

Tagged releases provide downloadable WCLAP archives and SHA-256 checksums:

https://github.com/charCulbert/wclap-test-plugins/releases

For browser hosts, GitHub Pages serves the latest `main` build with CORS enabled:

```text
https://charculbert.github.io/wclap-test-plugins/gui-lifecycle.wclap.tar.gz
https://charculbert.github.io/wclap-test-plugins/process-lifecycle.wclap.tar.gz
```

Release downloads are immutable build records, while the Pages URL is intended for convenient live testing.

## Build WCLAP

Requirements: CMake 3.24 or newer and [WASI SDK](https://github.com/WebAssembly/wasi-sdk). Version 33 is used in CI.

```sh
git clone --recurse-submodules https://github.com/charCulbert/wclap-test-plugins.git
cd wclap-test-plugins
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/wasi-sdk/share/cmake/wasi-sdk.cmake
cmake --build build
```

The root-layout archive is written to:

```text
build/artifacts/gui-lifecycle.wclap.tar.gz
build/artifacts/process-lifecycle.wclap.tar.gz
```

## Build native CLAP

Configure without a Wasm toolchain. The same `Plugin.cpp` and `Entry.cpp` are used:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

The native plug-in is written under `build-native/artifacts/`.

## Dependencies

- [`free-audio/clap`](https://github.com/free-audio/clap)
- [`free-audio/clap-helpers`](https://github.com/free-audio/clap-helpers)
- [`free-audio/clap-wrapper`](https://github.com/free-audio/clap-wrapper)

All dependencies are pinned Git submodules from their upstream repositories. The fixture source is MIT licensed, and release archives include applicable third-party licenses.

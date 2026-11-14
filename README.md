# WCLAP test plug-ins

Small CLAP plug-ins that each check one piece of host behaviour. Every plug-in builds from the same source as a WCLAP
(CLAP compiled to WebAssembly) and as a native CLAP, so a host can be tested in the browser and natively against
identical code.

## Plug-ins

| Plug-in | Proves |
| --- | --- |
| [`gui-lifecycle`](plugins/gui-lifecycle/README.md) | The host opens an embedded WebView interface in CLAP order (`create`, `get_size`, `set_parent`, `show`) and serves the plug-in's multi-file resources to it. |
| [`midi-echo`](plugins/midi-echo/README.md) | The host sends notes and raw MIDI to a plug-in in the dialect its note input asks for (MIDI or MIDI 2), passes sysex through, sets the event flags it means to, and receives the note and MIDI events a plug-in sends out. |
| [`note-ids`](plugins/note-ids/README.md) | The host passes its note IDs through to the plug-in, ends notes by ID, key, or a wildcard (`-1`) that matches every voice on a key, delivers note chokes, and receives `CLAP_EVENT_NOTE_END` with the address of the note-on. |
| [`note-name`](plugins/note-name/README.md) | The host reads a plug-in's note names, including a name for any key (`-1`), and reads them again when the plug-in calls `note_name.changed`. |
| [`param-indication`](plugins/param-indication/README.md) | The host tells a plug-in, through `param_indication`, when a parameter is mapped to a control and what state its automation is in, with the colours it shows them in, and clears both when they end. |
| [`probe`](plugins/probe/README.md) | The host delivers note, parameter, modulation and note-expression events on their exact frame, with their exact values, to the right voices. |
| [`process-lifecycle`](plugins/process-lifecycle/README.md) | The host honours every CLAP process status, wakes a sleeping plug-in on `request_process` and on due events, advances `steady_time` across skipped blocks, and reports finite and infinite tails (how long a plug-in keeps sounding after its input stops). |
| [`remote-controls`](plugins/remote-controls/README.md) | The host reads a plug-in's remote-control pages, maps each control to the parameter the page names, and reads the pages again when the plug-in calls `remote_controls.changed`. |
| [`render`](plugins/render/README.md) | The host calls `render.set` with `CLAP_RENDER_OFFLINE` before it renders faster or slower than real time, and with `CLAP_RENDER_REALTIME` when it goes back to playing in real time. |
| [`state-context`](plugins/state-context/README.md) | The host saves and loads a plug-in's state through `state_context` with the context that matches what it is doing: `CLAP_STATE_CONTEXT_FOR_PRESET` for a preset, `CLAP_STATE_CONTEXT_FOR_DUPLICATE` when it copies the plug-in, and `CLAP_STATE_CONTEXT_FOR_PROJECT` for a project. |
| [`voice-info`](plugins/voice-info/README.md) | The host reads a plug-in's voice info while it is active and reads it again when the plug-in calls `voice_info.changed`. |

Each plug-in's README says what a correct host does with it: load it, reject it, or contain it (stop processing it
safely; see process-lifecycle's README).

Plug-in IDs are `com.charlieculbert.wclap-test-plugins.<name>`. `midi-echo` also holds a second plug-in,
`com.charlieculbert.wclap-test-plugins.midi-echo.midi2`.

## Download

Tagged [releases](https://github.com/charCulbert/wclap-test-plugins/releases) carry, for each plug-in:

- `<name>.wclap.tar.gz`: the WCLAP, with `module.wasm` and any resources at the archive root
- `<name>-macos.clap.zip`, `<name>-linux-x64.clap.zip`, `<name>-windows-x64.clap.zip`: native builds
- `manifest.json`: the git commit and tag, and per plug-in its ID, version, what it proves, and each file's platform,
  SHA-256 and size
- `SHA256SUMS`

GitHub Pages serves the same set for the latest `main` build, with CORS enabled for browser hosts:

```text
https://charculbert.github.io/wclap-test-plugins/
https://charculbert.github.io/wclap-test-plugins/manifest.json
https://charculbert.github.io/wclap-test-plugins/gui-lifecycle.wclap.tar.gz
```

Releases are fixed records; the Pages URLs change with every push to `main`.

## Build

Requirements: CMake 3.24 or newer and a C++17 compiler. Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/charCulbert/wclap-test-plugins.git
cd wclap-test-plugins
```

### WCLAP

Install [WASI SDK](https://github.com/WebAssembly/wasi-sdk) (CI uses version 33), then:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/wasi-sdk/share/cmake/wasi-sdk.cmake
cmake --build build
```

Archives are written to `build/artifacts/<name>.wclap.tar.gz`. Their `module.wasm` is stripped of debug info; the
unstripped module stays in `build/artifacts/<name>.wclap/`.

### Native CLAP

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

Plug-ins are written to `build-native/artifacts/<name>.clap`.

## Add a plug-in

1. Create `plugins/<name>/` with:
   - `Plugin.h`, `Plugin.cpp`: the plug-in, in namespace `wclap_test_plugins::<name>`. Use `TEST_PLUGIN_ID`,
     `TEST_PLUGIN_VERSION` and `TEST_PLUGIN_URL` in the descriptor; CMake defines them.
   - `Entry.cpp`: the `clap_entry` export.
   - `README.md`: a `## Proves` heading followed by one paragraph, then what a correct host does (load, reject or
     contain). The release manifest reads the Proves paragraph.
   - `resources/` (optional): files bundled next to the module.
   - `tests/*.cpp` (optional): each file becomes a native `ctest` test linked against the plug-in.
2. Add a row to the table above.

The root `CMakeLists.txt` picks up every directory in `plugins/` that has a `Plugin.cpp` and builds it with
`add_test_plugin()` from [`cmake/AddTestPlugin.cmake`](cmake/AddTestPlugin.cmake). Use the existing plug-ins as
templates.

## Licence

The plug-ins are MIT licensed; see [LICENSE](LICENSE). They build on
[`free-audio/clap`](https://github.com/free-audio/clap),
[`free-audio/clap-helpers`](https://github.com/free-audio/clap-helpers) and
[`free-audio/clap-wrapper`](https://github.com/free-audio/clap-wrapper), pinned as Git submodules. WCLAP archives
include those licences and the ones for wasi-libc and libc++ (kept in [`licenses/`](licenses/)) under
`THIRD_PARTY_LICENSES/`.

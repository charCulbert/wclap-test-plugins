# WCLAP test plug-ins

Small CLAP plug-ins that each check one piece of host behaviour. Every plug-in builds from the same source as a WCLAP
(CLAP compiled to WebAssembly) and as a native CLAP, so a host can be tested in the browser and natively against
identical code.

## Plug-ins

| Plug-in | Proves |
| --- | --- |
| [`audio-ports-activation`](plugins/audio-ports-activation/README.md) | The host activates and deactivates a plug-in's audio ports through `audio_ports_activation` only while the plug-in is inactive, keeps passing buffers for deactivated ports, and reads their silence and `constant_mask`. |
| [`crash`](plugins/crash/README.md) | How the host contains a plug-in that traps inside `process()`: it keeps running, stops calling the dead instance, and either passes the input through, when the plug-in's input and output ports match, or goes silent. |
| [`gui-lifecycle`](plugins/gui-lifecycle/README.md) | The host opens an embedded WebView interface through `clap.webview/3`, serves the plug-in's multi-file resources to it, and carries messages between the page and the plug-in. |
| [`log`](plugins/log/README.md) | The host takes `log` calls from the audio thread and the main thread, at every severity, with UTF-8 text, and carries on running the plug-in. |
| [`midi-echo`](plugins/midi-echo/README.md) | Which dialect the host sends notes and raw MIDI in when a plug-in's note input lists only one (MIDI, MIDI 2 or CLAP), that it passes sysex through and sets the event flags it means to, and that it receives the note and MIDI events a plug-in sends out. |
| [`note-ids`](plugins/note-ids/README.md) | The host passes its note IDs through to the plug-in, ends notes by ID, key, or a wildcard (`-1`) that matches every voice on a key, delivers note chokes, and receives `CLAP_EVENT_NOTE_END` with the address of the note-on. |
| [`note-name`](plugins/note-name/README.md) | The host reads a plug-in's note names, including a name for any key (`-1`), and reads them again when the plug-in calls `note_name.changed`. |
| [`param-indication`](plugins/param-indication/README.md) | The host tells a plug-in, through `param_indication`, when a parameter is mapped to a control and what state its automation is in, with the colours it shows them in, and clears both when they end. |
| [`probe`](plugins/probe/README.md) | The host delivers note, parameter, modulation and note-expression events on their exact frame, with their exact values, to the right voices. |
| [`process-lifecycle`](plugins/process-lifecycle/README.md) | The host honours every CLAP process status, wakes a sleeping plug-in on due events and on `request_process`, advances `steady_time` across skipped blocks, and reads finite and infinite tails (how long a plug-in keeps sounding after its input stops) when the plug-in reports a change. |
| [`remote-controls`](plugins/remote-controls/README.md) | The host reads a plug-in's remote-control pages, maps each control to the parameter the page names, and reads the pages again when the plug-in calls `remote_controls.changed`. |
| [`render`](plugins/render/README.md) | Whether the host calls `render.set`: with `CLAP_RENDER_OFFLINE` before it renders faster or slower than real time, and with `CLAP_RENDER_REALTIME` when it goes back to playing in real time. |
| [`state-context`](plugins/state-context/README.md) | Which call and context the host uses to save and load a plug-in's state: `state_context` with `CLAP_STATE_CONTEXT_FOR_PRESET` for a preset, `CLAP_STATE_CONTEXT_FOR_DUPLICATE` when it copies the plug-in, and `CLAP_STATE_CONTEXT_FOR_PROJECT` for a project, or plain `state`. |
| [`transport`](plugins/transport/README.md) | The host passes its transport (position, tempo, bar, time signature, play and loop state) in each block's `transport`, and any change inside a block as a `CLAP_EVENT_TRANSPORT` event on the frame it happens, with the values it means. |
| [`voice-info`](plugins/voice-info/README.md) | The host reads a plug-in's voice info while it is active and reads it again when the plug-in calls `voice_info.changed`. |

Each plug-in's README says what a correct host does with it. Every plug-in is meant to be loaded. Only what the CLAP
headers require is pass/fail, with the header cited; where CLAP leaves a choice to the host, the README says what each
choice shows. process-lifecycle and audio-ports-activation also say when a host may stop processing a plug-in after
`CLAP_PROCESS_ERROR` (contain it).

No plug-in ends the host's process on a call it does not expect. A call made in the wrong state or with an unknown ID
or index is refused (the plug-in returns `false`, `0` or nothing) and reported through the host's `clap.log`, if it
has one, at `CLAP_LOG_HOST_MISBEHAVING`. Each README names the refusals that matter for its test.
The one deliberate exception is `crash`: setting its `Crash` parameter to `1` traps inside `process()`, which ends a
native host's process. Load its native builds only to check behaviour before the crash, and keep it out of parameter
fuzzing.

Plug-in IDs are `com.charlieculbert.wclap-test-plugins.<name>`. `midi-echo` also holds two more plug-ins,
`com.charlieculbert.wclap-test-plugins.midi-echo.midi2` and `com.charlieculbert.wclap-test-plugins.midi-echo.clap`.
`crash` also holds `com.charlieculbert.wclap-test-plugins.crash.instrument`.

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
   - `README.md`: a `## Proves` heading followed by one paragraph, then what a correct host does: what CLAP requires
     (citing the header) and what each host choice shows. The release manifest reads the Proves paragraph.
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

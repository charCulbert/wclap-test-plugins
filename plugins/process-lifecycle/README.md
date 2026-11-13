# process-lifecycle

A GUI-free utility with two mono outputs and one note input. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.process-lifecycle`

## Proves

The host honours every CLAP process status, wakes a sleeping plug-in on `request_process` and on due events, advances `steady_time` across skipped blocks, and reports finite and infinite tails.

## A correct host

**Loads** the plug-in and follows the protocol below: skipped blocks are cleared to silence, a due note-on wakes the plug-in, and the `9.0` poison block from `CLAP_PROCESS_ERROR` never reaches the output. After that error block it may stop processing the plug-in (**contain**), but must not pass its output on. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side of each step (statuses, markers, tail lengths, wake requests) on the native build under `ctest`.

## Ports and markers

The plug-in has no audio inputs, two mono audio outputs, and one CLAP/MIDI note input.

| Output | Meaning |
| --- | --- |
| 0, `Status signal` | Constant marker for each real `process()` call |
| 1, `Steady time / 65536` | `clamp(steady_time / 65536, 0, 1)` |

On its first call the plug-in writes `0.1`, requests `host.request_callback()`, and returns `CLAP_PROCESS_SLEEP`. In `on_main_thread()` it calls `host.request_process()`; that wake call writes `0.2` and sleeps again.

MIDI note-ons (any channel, nonzero velocity) and equivalent CLAP note-ons select the remaining behavior:

| Key | Output 0 | Return/tail behavior |
| --- | ---: | --- |
| 60 | `0.3` | `CLAP_PROCESS_TAIL`, finite 256-sample tail |
| 61 | `0.4`, then `0.0` | `CLAP_PROCESS_CONTINUE_IF_NOT_QUIET`; the second call makes **both** outputs exactly quiet |
| 62 | `0.0` | `CLAP_PROCESS_SLEEP` |
| 63 | `0.5` | `CLAP_PROCESS_CONTINUE` |
| 64 | `0.6` | `CLAP_PROCESS_TAIL`, infinite (`>= INT32_MAX`) tail |
| 65 | poison `9.0` on both outputs | `CLAP_PROCESS_ERROR`; the host must discard/clear the poison block |

The steady-time output is intentionally zero during key 61's quiet block so it does not keep `CONTINUE_IF_NOT_QUIET` awake.

## Validation protocol

Use an offline render of at least 5248 frames with a 128-frame quantum and route both outputs to separate captured channels. Service requested main-thread callbacks between quanta, and retain lifecycle diagnostics (processed/skipped blocks, status, sleeping state, and wake source) when available. Schedule MIDI note-ons at these exact frames:

| Frame | Key | Expected result |
| ---: | ---: | --- |
| 1024 | 63 | Event wakes sleep: `0.5`; steady output is exactly `1024 / 65536` |
| 1152 | 62 | Enter sleep; later quanta are skipped and cleared |
| 2048 | 60 | Event wakes sleep; `0.3` from frames 2048–2431 (entry block plus the 256-sample finite tail), then skip |
| 3072 | 64 | Event wakes sleep; `0.6` continues beyond 256 samples |
| 3584 | 62 | Due event ends infinite tail and enters sleep |
| 4096 | 61 | Event wakes sleep; one audible `0.4` quiet-aware block |
| 4608 | 63 | Wakes after the all-zero block at 4224 and skipped blocks; steady output is `4608 / 65536` |
| 4736 | 62 | Enter sleep again |
| 5120 | 65 | Event wakes sleep and returns `CLAP_PROCESS_ERROR`; captured output must be zero, never `9.0` |

The startup blocks are `0.1` at frames 0–127 and `0.2` at frames 128–255. Confirm exactly one `request_callback`/`on_main_thread` round trip causes exactly one process-request wake, then frames 256–1023 are skipped and cleared. If a host services the callback later, shift only the `0.2` block and begin scheduled controls after it.

For key 61, frames 4096–4223 are audible. Frames 4224–4351 are an actually processed all-zero `CLAP_PROCESS_CONTINUE_IF_NOT_QUIET` block; frames 4352–4607 are skipped. Diagnostics must distinguish that quiet process call from the skipped blocks. The later steady markers prove skipped time advanced `steady_time`. Tail diagnostics must report 256 for key 60 and infinity (`>= INT32_MAX`) for key 64. A host may terminate or reject the processing path after clearing the final error block.

A due input event itself must wake sleep; no parameter or WebView control path is involved.

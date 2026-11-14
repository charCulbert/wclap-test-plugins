# process-lifecycle

A GUI-free utility with two mono outputs and one note input. Native CLAP and WCLAP compile the same
[`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.process-lifecycle`

## Proves

The host honours every CLAP process status, wakes a sleeping plug-in on due events and on `request_process`, advances
`steady_time` across skipped blocks, and reads finite and infinite tails (how long a plug-in keeps sounding after its
input stops) when the plug-in reports a change.

## A correct host

**Loads** the plug-in and follows the protocol below. Its output never contains the `9.0` from the
`CLAP_PROCESS_ERROR` block: [`process.h`](../../external/clap/include/clap/process.h) says the output of a failed call
"must be discarded". After that block it may stop processing the plug-in (**contain**). Whether it skips calls while
the plug-in sleeps or its tail has run out is its own choice; the protocol says what each choice shows.
[`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side of each step (statuses, markers, steady-time advances,
tail lengths, `tail.changed` calls, wake requests, `reset`), once with MIDI note-ons and once with CLAP note-ons, on the
native build under `ctest`.

## Ports and markers

The plug-in has no audio inputs, two mono audio outputs, and one note input.

| Output | Meaning |
| --- | --- |
| 0, `Status signal` | Constant marker for each real `process()` call |
| 1, `Steady time advance / 65536` | How far `steady_time` moved since the previous `process()` call, over 65536 |

The note input, `Lifecycle controls`, takes CLAP and MIDI notes and prefers MIDI. The preference is only a hint
([`ext/note-ports.h`](../../external/clap/include/clap/ext/note-ports.h)), so a host may send either; a MIDI note-on
(any channel, non-zero velocity) and a CLAP note-on with the same key do the same thing.

On its first call after `activate()` the plug-in writes `0.1`, requests `host.request_callback()`, and returns
`CLAP_PROCESS_SLEEP`. In `on_main_thread()` it calls `host.request_process()`; that wake call writes `0.2` and sleeps
again. If that call also carries a note-on that selects a mode, the mode's marker and status win and no `0.2` block
appears. Each activation starts the protocol over, in sleep mode.

Note-ons select the remaining behaviour:

| Key | Output 0 | Return/tail behaviour |
| --- | ---: | --- |
| 60 | `0.3` | `CLAP_PROCESS_TAIL`, finite 256-sample tail |
| 61 | `0.4`, then `0.0` | `CLAP_PROCESS_CONTINUE_IF_NOT_QUIET`; the second call makes **both** outputs exactly quiet |
| 62 | `0.0` | `CLAP_PROCESS_SLEEP` |
| 63 | `0.5` | `CLAP_PROCESS_CONTINUE` |
| 64 | `0.6` | `CLAP_PROCESS_TAIL`, infinite (`>= INT32_MAX`) tail |
| 65 | `9.0` on both outputs | `CLAP_PROCESS_ERROR` |

A mode stays until the next note-on changes it, and the plug-in returns the same status on every call in that mode.

The tail is 256 in key 60's mode, `INT32_MAX` in key 64's and 0 otherwise. Whenever it changes, the plug-in calls the
host's `tail.changed` from `process()` ([`ext/tail.h`](../../external/clap/include/clap/ext/tail.h): "Tell the host
that the tail has changed", `[audio-thread]`), if the host provides `clap.tail`. `tail.get` answers whether the plug-in
is active or not: the header lets the host call it on the main thread or the audio thread with no active requirement.

### Steady-time output

`steady_time` must be `-1` (not available) or at least `0`, and must grow by at least `frames_count` from one call to
the next ([`process.h`](../../external/clap/include/clap/process.h), comment on `steady_time`). It need not start at
`0`. Output 1 shows the advance since the previous call, so it does not depend on the starting value:

| Output 1 | Meaning |
| --- | --- |
| `0` | First call after `activate()`, or `steady_time` is `-1` on this call or the previous one |
| `advance / 65536` | The advance, capped at `1.0` (65536 frames) |
| `-1` | The rule is broken: `steady_time` below `-1`, or it grew by less than the previous call's `frames_count` |

`reset()` starts the comparison over, so the next call shows `0`: [`plugin.h`](../../external/clap/include/clap/plugin.h)
says `steady_time` "may jump backward" after a reset.

The header says `steady_time` "can be used to calculate the sleep duration between two process calls". A host whose
counter keeps running while it skips calls shows the skipped frames in the advance; one that counts only processed
frames shows `128 / 65536` after a gap. Both meet the rule; `-1` anywhere fails it. Output 1 is zero during key 61's
quiet block, so it does not keep `CONTINUE_IF_NOT_QUIET` awake.

## Validation protocol

Use an offline render of at least 5248 frames with a 128-frame block (the render quantum) and route both outputs to
separate captured channels. Run the requested main-thread callbacks between blocks, and record lifecycle diagnostics
(processed and skipped blocks, status, sleeping state, and wake source) when available. Run it twice: once with MIDI
note-ons with non-zero velocity, once with CLAP note-ons on note port 0. Both runs must give the same output. Schedule note-ons at these exact
frames:

| Frame | Key | Status returned | Output 0 |
| ---: | ---: | --- | --- |
| 1024 | 63 | `CLAP_PROCESS_CONTINUE` | `0.5` |
| 1152 | 62 | `CLAP_PROCESS_SLEEP` | `0` |
| 2048 | 60 | `CLAP_PROCESS_TAIL`, tail 256 | `0.3` |
| 3072 | 64 | `CLAP_PROCESS_TAIL`, tail `>= INT32_MAX` | `0.6` until frame 3583 |
| 3584 | 62 | `CLAP_PROCESS_SLEEP` | `0` |
| 4096 | 61 | `CLAP_PROCESS_CONTINUE_IF_NOT_QUIET` | `0.4` for frames 4096–4223, then `0` |
| 4608 | 63 | `CLAP_PROCESS_CONTINUE` | `0.5` |
| 4736 | 62 | `CLAP_PROCESS_SLEEP` | `0` |
| 5120 | 65 | `CLAP_PROCESS_ERROR` | anything but `9.0` |

Pass/fail, whatever the host chooses below:

- Every note-on is processed on its frame, including when the plug-in sleeps. `CLAP_PROCESS_SLEEP` means no more
  processing is required "until the next event", so a due event wakes it.
- Output 1 is never `-1`.
- Neither output contains `9.0`. After the error block the host may stop processing the plug-in.
- The tail the host reads is 256 after key 60 and `>= INT32_MAX` after key 64. The plug-in calls `tail.changed` at
  2048, 3072 and 3584 (back to 0).

Host choices and what each shows:

- **Startup.** The `0.1` block is frames 0–127. One `request_callback`/`on_main_thread` round trip leads to one
  `request_process`. [`host.h`](../../external/clap/include/clap/host.h) calls it a request ("Request the host to
  activate and start processing the plugin"), so honouring it is the expected behaviour but not a requirement. A host
  that honours it shows the `0.2` block as the first block after it services the callback: frames 128–255 if it does so
  between the first two blocks. A host that services it later shifts only the `0.2` block; one that ignores the request
  shows no `0.2` block.
- **Sleep (`CLAP_PROCESS_SLEEP`, keys 62 and startup).** CLAP allows the host to skip calls until the next event; it
  does not require it. A host that skips shows silence for the skipped blocks. One that keeps processing shows `0`
  there too, from real calls, which its diagnostics tell apart.
- **Finite tail (key 60).** A host that stops after the tail runs out shows `0.3` and then silence. If it counts the
  256 samples from the end of the block that returned `CLAP_PROCESS_TAIL`, `0.3` covers frames 2048–2431; if it counts
  from the start of that block, frames 2048–2303. Both pass. A host that keeps processing shows `0.3` until frame 3071.
- **Quiet-aware (key 61).** Frames 4224–4351 are a processed all-zero block. A host that applies
  `CONTINUE_IF_NOT_QUIET` then skips until frame 4608; one that keeps processing shows `0` from real calls.
- **Steady time.** With a counter that keeps running while it skips, output 1 on the note-on at frame 1024 is
  `896 / 65536` (the advance from the `0.2` block at frame 128), and on the note-on at frame 4608 it is
  `384 / 65536` (from the quiet block at frame 4224). Between processed calls with no gap it is `128 / 65536`.

No parameter or WebView control path is involved.

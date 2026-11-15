# crash

A GUI-free plug-in that crashes on request. Inside `process()` it executes a trap the moment the host sets its `Crash`
parameter to `1` with a parameter event, or on the next `process()` call when the value arrives through `params.flush`.
Until then the effect passes its input through and the instrument writes a constant. In a WCLAP the
trap is a real WebAssembly trap (`unreachable`), so a host test can check how the host contains a plug-in that dies
mid-block. The bundle holds two plug-ins that differ only in their ports. Native CLAP and WCLAP compile the same
[`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

| Plug-in ID | Name | Audio ports | Output before the crash |
| --- | --- | --- | --- |
| `com.charlieculbert.wclap-test-plugins.crash` | WCLAP Crash Test | Stereo input, stereo output | The input, unchanged |
| `com.charlieculbert.wclap-test-plugins.crash.instrument` | WCLAP Crash Instrument Test | Stereo output only | `0.5` on both channels |

## Proves

How the host contains a plug-in that traps inside `process()`: it keeps running, stops calling the dead instance, and
either passes the input through, when the plug-in's input and output ports match, or goes silent.

## A correct host

**Loads** both plug-ins, plays them, and then sets `Crash` to `1`. The CLAP headers say nothing about a plug-in that
crashes, so what follows is the host's choice, not a pass or fail against a header. What each choice shows:

- **Keeps running.** A WCLAP trap stops the WebAssembly call and nothing else, so a host that catches it can carry on.
  One that lets it end the host's process, or its audio thread, shows no output from anything after it.
- **Stops calling the instance.** After a trap the module's memory is in whatever state the trap left it, so a host
  should not call `process()` or anything else on that instance again, apart from discarding it. Treat the trapping
  block as [`process.h`](../../external/clap/include/clap/process.h) treats `CLAP_PROCESS_ERROR`: its output "must be
  discarded".
- **Passes the input through or goes silent.** The effect's input and output are both one stereo main port, so a host
  can bypass it and pass its input straight to its output from the trapping block on. The instrument has no input, so
  there is nothing to pass through, and a host goes silent from the trapping block on. A host that keeps playing the
  instrument's last good block, or anything else that is not silence, is wrong.

A native `.clap` cannot be contained this way: on a native build the trap (`__builtin_trap()`, or `std::abort()` with
MSVC) ends the host's process unless the host runs the plug-in in a separate process. Load the native builds only to
check behaviour before the crash. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-ins' side of everything up to the
crash (ports, the parameter, text conversion, the effect's pass-through in and out of place, the instrument's constant,
values that do not set `Crash`, `params.flush`) on the native build under `ctest`. It never sets `Crash` to `1` in
`process()`, so it never traps.

## Ports and parameter

| Plug-in | Port | Channels | Use |
| --- | --- | --- | --- |
| `crash` | Audio input 0, `Input` | 2 | Main input |
| `crash` | Audio output 0, `Input, unchanged` | 2 | Main output; in-place pair of input 0 |
| `crash.instrument` | Audio output 0, `Constant 0.5` | 2 | Main output |

Neither plug-in has a note port. Both return `CLAP_PROCESS_CONTINUE` on every block that does not trap and has the
ports above with buffers, and `CLAP_PROCESS_ERROR` otherwise.

One parameter, `Crash`: ID `0`, range 0 to 1, default 0, stepped, automatable. Its text is `Off` for 0 and `Crash` for
1, and `text_to_value` takes only those two words. Being stepped, a value converts to a whole number by truncation
([`ext/params.h`](../../external/clap/include/clap/ext/params.h): "converted to integer using a cast (equivalent to
trunc)"), then clamps to 0 to 1. So only values of `1` or more, such as `1` or `1.7`, set it to `1`; anything below
`1`, including `0.5`, `0.99` and negative values, sets it to `0`, and so does NaN. `params.get_value` returns the
converted value.

## Output

Before the crash:

| Plug-in | Left | Right |
| --- | --- | --- |
| `crash` | Input left, sample for sample | Input right, sample for sample |
| `crash.instrument` | `0.5` | `0.5` |

`0.5` is exact in a 32-bit float, and the effect copies its input without changing a bit, so compare exactly.

**The trap.** `process()` traps:

- on the first `CLAP_EVENT_PARAM_VALUE` in its input events that sets `Crash` to `1` (a value of `1` or more),
  whatever its frame, after writing the block's output; or
- at the start of the call, before it writes anything, when `Crash` is already `1`: set through `params.flush` before
  the block. `params.flush` stores the value and does not trap.

A trapped call does not return, so the block it was writing has no defined output. Other events (`PARAM_MOD`, another
parameter ID, notes) never trap.

## Validation protocol

Run it once with each plug-in, as a WCLAP.

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load, activate and play. The effect gets an input signal. | `crash`: the input. `crash.instrument`: `0.5` on both channels. |
| 2 | Set `Crash` to `0` | No change |
| 3 | Set `Crash` to `1` | The plug-in traps in `process()` within the block that carries the change. The host keeps running. |
| 4 | Keep playing | `crash`: the input, if the host bypasses it, or silence. `crash.instrument`: silence. The host makes no more calls to the instance except to destroy it. |
| 5 | Remove the plug-in and load it again | It plays as in step 1, with `Crash` back at `0` |

## Validators

A validator that fuzzes parameters sets `Crash` to `1`, and the plug-in traps, as designed. Exclude this plug-in from
parameter fuzzing. CI does not run a validator; it builds the plug-ins and runs `ctest`, whose smoke test never sets
`Crash` to `1` in `process()`.

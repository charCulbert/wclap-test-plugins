# audio-ports-activation

A GUI-free instrument with two mono outputs and no inputs. Each output it keeps active carries its own marker, and
each output the host deactivates through `audio_ports_activation` carries silence with its `constant_mask` set. Native
CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no
Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.audio-ports-activation`

## Proves

The host activates and deactivates a plug-in's audio ports through `audio_ports_activation` only while the plug-in is
inactive, keeps passing buffers for deactivated ports, and reads their silence and `constant_mask`.

## A correct host

**Loads** the plug-in. To stop consuming output 2 it deactivates the plug-in, calls
`set_active(false, 1, false, 32)` (or sample size `0`), activates it again, and still passes a buffer for output 2,
which then reads `0` with `constant_mask` bit 0 set. Output 1 keeps reading `1`. Since
`can_activate_while_processing` returns `false`, a host that calls `set_active` while the plug-in is active breaks the
CLAP contract: the plug-in refuses the call (returns `false`), reports it through the host's `log` extension as host
misbehaviour, and keeps its ports as they were. A host that passes no buffer for a deactivated port gets
`CLAP_PROCESS_ERROR` and must **contain** the plug-in. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side
(ports, each combination of active outputs, refused calls, the refusal while active) on the native build under `ctest`.

## Ports and extension

| Port | Channels | Active | Inactive |
| --- | --- | ---: | ---: |
| Audio output 0, `Output 1` (main) | 1 | `1` | `0` |
| Audio output 1, `Output 2` | 1 | `2` | `0` |

There is no audio input, no note port and there are no parameters. Both outputs start active, and a port's state
survives reactivation; it is not saved in the plug-in's state. The plug-in returns `CLAP_PROCESS_CONTINUE` from every
block, or `CLAP_PROCESS_ERROR` if it gets other than two mono 32-bit output buffers.

| Call | Result |
| --- | --- |
| `can_activate_while_processing()` | `false` |
| `set_active(false, 0 or 1, on, 0 or 32)` while inactive | `true`; the output follows `on` from the next activation |
| `set_active` with `is_input` true, port index 2 or more, or sample size 64 | `false`, nothing changes |
| `set_active` while active | `false`, nothing changes, host misbehaviour logged |

## Output

Every frame of an output holds the same value and both outputs' `constant_mask` bit 0 is set, active or not.

## Validation protocol

| Step | Host action | Output 1 | Output 2 |
| ---: | --- | ---: | ---: |
| 1 | Load, activate and render | `1` | `2` |
| 2 | Deactivate the plug-in, deactivate output 2, activate and render | `1` | `0` |
| 3 | Deactivate the plug-in, deactivate output 1, activate and render | `0` | `0` |
| 4 | Deactivate the plug-in, activate both outputs, activate and render | `1` | `2` |

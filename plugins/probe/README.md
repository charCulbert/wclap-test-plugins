# probe

A GUI-free instrument with one stereo output, one stereo input it ignores, one CLAP/MIDI note input and one parameter.
It writes every event it receives into the output on the event's frame, so a host test can check timing and values
from the rendered audio alone. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and
[`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.probe`

## Proves

The host delivers note, parameter, modulation and note-expression events on their exact frame, with their exact values,
to the right voices.

## A correct host

**Loads** the plug-in. Each event it schedules shows up in the output on the frame it was scheduled for, with the value
the table below predicts. An event that arrives a frame early or late, rounded, or addressed to the wrong voice shows up
as a wrong sample. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (ports, parameter flags, every row
below, `params.flush`) on the native build under `ctest`.

## Ports and parameter

| Port | Channels | Use |
| --- | --- | --- |
| Audio input 0, `Ignored input` | 2 | Never read |
| Audio output 0, `Clicks and voices / parameter` | 2 | Left: clicks plus voice sum. Right: parameter. |
| Note input 0, `Notes` | | CLAP and MIDI dialects; CLAP preferred |

One parameter, `Level`: ID `1`, range 0 to 1, default 0. Flags: automatable, modulatable, and modulatable per note ID,
key, channel and port. It is not automatable per voice.

The plug-in sends no output events, and returns `CLAP_PROCESS_CONTINUE` from every block.

## Output

Both channels hold their value until the next event changes it. An event takes effect on its own frame: on that frame
and every later one. An event stamped at or past the end of the block is applied on the block's last frame.

**Right channel:**

```text
right = Level + global modulation
```

Neither term is clamped, so an out-of-range value or modulation shows as it arrived.

**Left channel:**

```text
left = click + sum over sounding voices of (voice modulation + voice tuning / 128)
```

`click` is non-zero only on the frame of a note event, and the clicks of several note events on one frame add up.

| Event | Effect |
| --- | --- |
| `CLAP_EVENT_NOTE_ON` | Click `+1.0`. Starts a voice with the event's note ID, port, channel and key, modulation 0 and tuning 0. A note-on with port, channel or key `-1` is invalid and is ignored (no click, no voice). |
| `CLAP_EVENT_NOTE_OFF`, `CLAP_EVENT_NOTE_CHOKE` | Click `-1.0`, whether or not a voice matches. Ends every matching voice. |
| MIDI note-on (`0x9n`, velocity > 0) | As `NOTE_ON` with note ID `-1`, channel `n` and the event's port. |
| MIDI note-off (`0x8n`, or `0x9n` with velocity 0) | As `NOTE_OFF` with note ID `-1`. |
| `CLAP_EVENT_PARAM_VALUE`, `Level`, all of note ID, port, channel and key `-1` | Sets `Level`. Also read back by `params.get_value`. |
| `CLAP_EVENT_PARAM_VALUE` addressed to a voice | Ignored, since per-voice automation is not advertised. |
| `CLAP_EVENT_PARAM_MOD`, `Level`, all address fields `-1` | Sets the global modulation (replaces, not adds). |
| `CLAP_EVENT_PARAM_MOD`, `Level`, any address field set | Sets the modulation of every matching voice. |
| `CLAP_EVENT_NOTE_EXPRESSION`, `CLAP_NOTE_EXPRESSION_TUNING` | Sets the tuning (semitones) of every matching voice. |
| Other note expressions and events | Ignored. |

A voice matches when each of the event's note ID, port, channel and key is `-1` or equal to the voice's. Voice state does
not carry over: a new voice starts with modulation and tuning 0 even if an earlier voice on the same key had them.
Activating or resetting the plug-in ends every voice and clears the global modulation; `Level` keeps its value. There
are 64 voices; a note-on with none free still clicks but starts no voice.

`params.flush` applies `PARAM_VALUE` and `PARAM_MOD` events the same way and ignores note events.

Values used in the protocol below are exact in 32-bit floats; compare other values within `1e-6`.

## Validation protocol

Render one block of at least 64 frames from a freshly activated plug-in and schedule these events on note port 0,
channel 0. Expected samples are for the frame range shown; `[a, b)` includes `a` and excludes `b`.

| Frame | Event | Left | Right |
| ---: | --- | --- | --- |
| 0 | | `[0, 4)`: `0` | `[0, 8)`: `0` |
| 4 | Note-on, note ID 7, key 60 | `[4]`: `1`, then `0` | |
| 8 | `Level` value `0.5` | | `[8, 12)`: `0.5` |
| 12 | `Level` modulation `0.25`, global | | `[12, 56)`: `0.75` |
| 16 | Note-on, note ID 8, key 62 | `[16]`: `1`, then `0` | |
| 20 | `Level` modulation `0.5`, note ID 7 | `[20, 24)`: `0.5` | |
| 24 | Tuning `12`, note ID 8 | `[24, 28)`: `0.59375` | |
| 28 | Tuning `32`, all fields `-1` | `[28, 32)`: `1.0` (both voices) | |
| 32 | Note-off, note ID `-1`, key 60 | `[32]`: `-0.75`, then `0.25` (note 7 ended) | |
| 36 | Note-choke, note ID 8, key 62 | `[36]`: `-1`, then `0` | |
| 40 | MIDI `0x91 64 100` | `[40]`: `1`, then `0` | |
| 44 | MIDI `0x91 64 0` | `[44]`: `-1`, then `0` | |
| 56 | `Level` modulation `0`, global | | `[56, end)`: `0.5` |

After the block, `params.get_value` for `Level` returns `0.5`.

A host whose per-voice modulation reaches every voice, or is sent as global modulation, shows `1.0` on the left or a
jump on the right at frame 20. A host that sends the wildcard note-off at frame 32 with note ID 7 still passes, since
that also matches note 7.

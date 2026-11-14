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
as a wrong sample. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (ports, preferred dialect, parameter
flags, every row below with CLAP notes and with MIDI notes, `NOTE_END` order, a note-on with no free voice, velocity 0
in each dialect, invalid note-ons, `params.flush`) on the native build under `ctest`.

## Ports and parameter

| Port | Channels | Use |
| --- | --- | --- |
| Audio input 0, `Ignored input` | 2 | Never read |
| Audio output 0, `Clicks and voices / parameter` | 2 | Left: clicks plus voice sum. Right: parameter. |
| Note input 0, `Notes` | | CLAP and MIDI dialects; CLAP preferred |

The preferred dialect is only a hint ([`ext/note-ports.h`](../../external/clap/include/clap/ext/note-ports.h)), so a
host may send notes in either. A MIDI note-on or note-off does exactly what a CLAP one with note ID `-1` does.

Velocity 0 means different things in the two dialects. In MIDI, `0x9n` with velocity 0 is a note-off, and the plug-in
treats it as one. A CLAP `NOTE_ON` with velocity 0 is still a note-on: [`events.h`](../../external/clap/include/clap/events.h)
says it "is valid and should not be interpreted as a NOTE_OFF".

One parameter, `Level`: ID `1`, range 0 to 1, default 0. Flags: automatable, modulatable, and modulatable per note ID,
key, channel and port, and `CLAP_PARAM_REQUIRES_PROCESS`, since it changes the output. It is not automatable per voice.

The plug-in returns `CLAP_PROCESS_CONTINUE` from every block. Its only output events are `CLAP_EVENT_NOTE_END`.

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
| `CLAP_EVENT_NOTE_ON` (any velocity, including 0) | Click `+1.0`. Starts a voice with the event's note ID, port, channel and key, modulation 0 and tuning 0. A note-on with port, channel or key `-1` is invalid and is ignored (no click, no voice). |
| `CLAP_EVENT_NOTE_OFF`, `CLAP_EVENT_NOTE_CHOKE` | Click `-1.0`, whether or not a voice matches. Ends every matching voice and sends a `NOTE_END` for each. |
| MIDI note-on (`0x9n`, velocity > 0) | As `NOTE_ON` with note ID `-1`, channel `n` and the event's port. |
| MIDI note-off (`0x8n`, or `0x9n` with velocity 0) | As `NOTE_OFF` with note ID `-1`: ends the voices on that port, channel and key. |
| `CLAP_EVENT_PARAM_VALUE`, `Level`, all of note ID, port, channel and key `-1` | Sets `Level`. Also read back by `params.get_value`. |
| `CLAP_EVENT_PARAM_VALUE` addressed to a voice | Ignored, since per-voice automation is not advertised. |
| `CLAP_EVENT_PARAM_MOD`, `Level`, all address fields `-1` | Sets the global modulation (replaces, not adds). |
| `CLAP_EVENT_PARAM_MOD`, `Level`, any address field set | Sets the modulation of every matching voice. |
| `CLAP_EVENT_NOTE_EXPRESSION`, `CLAP_NOTE_EXPRESSION_TUNING` | Sets the tuning (semitones) of every matching voice. |
| Other note expressions and events | Ignored. |

A voice matches when each of the event's note ID, port, channel and key is `-1` or equal to the voice's. Voice state does
not carry over: a new voice starts with modulation and tuning 0 even if an earlier voice on the same key had them.
Events apply in list order, so a per-voice `PARAM_MOD` or a note expression listed before the `NOTE_ON` it addresses,
even on the same frame, finds no voice and is lost. [`events.h`](../../external/clap/include/clap/events.h) says a
plug-in that gets a note expression at the same sample as a `NOTE_ON` "should apply that expression to all generated
samples"; this plug-in does that only when the expression comes after the note-on in the list.
Activating or resetting the plug-in ends every voice; `Level` and the global modulation keep their values. There
are 64 voices and none is ever stolen. A note-on with none free still clicks, starts no voice, and gets its `NOTE_END`
at once, on its own frame:
the host keeps a voice for every note-on until the plug-in sends `NOTE_END`
([`events.h`](../../external/clap/include/clap/events.h), comment above `CLAP_EVENT_NOTE_ON`).

Each ended voice produces one `CLAP_EVENT_NOTE_END` on the frame of the event that ended it, carrying the voice's own
note ID, port, channel and key from its note-on (not the note-off's). A voice started by MIDI has note ID `-1`. When one
event ends several voices, their `NOTE_END` events come out in the order their note-ons arrived, whichever voice slots
they used. A note-off that matches
nothing sends none. Activating or resetting the plug-in ends voices without sending `NOTE_END`.

`params.flush` applies `PARAM_VALUE` and `PARAM_MOD` events the same way and ignores note events.

Values used in the protocol below are exact in 32-bit floats; compare other values within `1e-6`.

## Validation protocol

Render one block of at least 64 frames from a freshly activated plug-in with `Level` and the global modulation at 0
(a new instance has both at 0), and schedule these events on
note port 0. Run it twice: once with the note events sent as CLAP note events (the CLAP run), once as MIDI (the MIDI
run). The parameter events are the same in both runs. The tuning (a note expression) and the choke exist only in the
CLAP dialect ([`ext/note-ports.h`](../../external/clap/include/clap/ext/note-ports.h): `CLAP_NOTE_DIALECT_CLAP` "uses
clap_event_note and clap_event_note_expression", MIDI has "no polyphonic expression"), so the MIDI run leaves out the
tunings at frames 24 and 28 and ends key 62 at frame 36 with a MIDI note-off instead of the choke. In the MIDI run,
voices have note ID `-1`, so per-voice events address them by channel and key with note ID `-1`. Expected samples are
for the frame range shown; `[a, b)` includes `a` and excludes `b`. Where the MIDI run differs, its value is given
after "MIDI:".

| Frame | Event | Left | Right | `NOTE_END` (channel, key) |
| ---: | --- | --- | --- | --- |
| 0 | | `[0, 4)`: `0` | `[0, 8)`: `0` | |
| 4 | Note-on, channel 0, key 60 | `[4]`: `1`, then `0` | | |
| 8 | `Level` value `0.5` | | `[8, 12)`: `0.5` | |
| 12 | `Level` modulation `0.25`, global | | `[12, 56)`: `0.75` | |
| 16 | Note-on, channel 0, key 62 | `[16]`: `1`, then `0` | | |
| 20 | `Level` modulation `0.5`, channel 0, key 60 | `[20, 24)`: `0.5` | | |
| 24 | Tuning `12`, channel 0, key 62 (CLAP run only) | `[24, 28)`: `0.59375`. MIDI: `0.5` | | |
| 28 | Tuning `32`, all fields `-1` (CLAP run only) | `[28, 32)`: `1.0` (both voices). MIDI: `0.5` | | |
| 32 | Note-off, channel 0, key 60 | `[32]`: `-0.75`, then `0.25` (key 60 ended). MIDI: `-1`, then `0` | | (0, 60) |
| 36 | Note-choke, channel 0, key 62. MIDI: note-off, channel 0, key 62 | `[36]`: `-1`, then `0` | | (0, 62) |
| 40 | Note-on, channel 1, key 64 | `[40]`: `1`, then `0` | | |
| 44 | Note-off, channel 1, key 64 | `[44]`: `-1`, then `0` | | (1, 64) |
| 46 | Note-on, channel 2, key 67 | `[46]`: `1` | | |
| 47 | Note-off, channel 2, key 67 | `[47]`: `-1`, then `0` | | (2, 67) |
| 56 | `Level` modulation `0`, global | | `[56, end)`: `0.5` | |

Leave the note ID of the modulation, the tuning, the note-off and the choke at `-1`, or set it to the note-on's: both
match. After the block, `params.get_value` for `Level` returns `0.5`, and the plug-in has sent exactly the four
`NOTE_END` events above, in that order, all on port 0. Each carries the note ID of its note-on: the host's own note ID
in the CLAP run, and `-1` in the MIDI run.

The table gives each `PARAM_MOD` event's literal amount. [`events.h`](../../external/clap/include/clap/events.h) says
that when a global and a polyphonic modulation meet, "the polyphonic modulation amount will already include the
monophonic signal". A host that follows that sends the frame-20 event with amount `0.75` (the global `0.25` plus `0.5`)
and shows, on the left, `0.75` at `[20, 24)`, `0.84375` at `[24, 28)` and `1.25` at `[28, 32)` in the CLAP run (`0.75`
at `[20, 32)` in the MIDI run); everything else is as in the table. Both are correct. A host whose per-voice modulation
reaches both voices shows `1.0` (`1.5` if folded) at `[20, 24)`; one that sends it as global modulation shows a jump on
the right at frame 20.

The smoke test also sends three events a correct host does not send, to check the plug-in's side: a note-on with key
`-1` at frame 48 (ignored: no click, no voice), a `Level` value addressed to a voice at frame 52 (ignored), and a
note-on stamped at frame 200 of a 128-frame block (applied on the last frame, `[127]`: `1`).

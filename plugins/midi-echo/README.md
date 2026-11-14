# midi-echo

A GUI-free note effect with one stereo output, one note input and one note output. It sends every note and MIDI event
it receives straight back out, and marks each one in the output with its event type and flags, so a host test sees the
dialect each event arrived in. The bundle holds three plug-ins that differ only in the dialect their note ports list.
Each lists exactly one. Native CLAP and WCLAP compile the
same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

| Plug-in ID | Name | Note ports: supported and preferred dialect |
| --- | --- | --- |
| `com.charlieculbert.wclap-test-plugins.midi-echo` | WCLAP MIDI Echo Test | MIDI |
| `com.charlieculbert.wclap-test-plugins.midi-echo.midi2` | WCLAP MIDI 2 Echo Test | MIDI 2 |
| `com.charlieculbert.wclap-test-plugins.midi-echo.clap` | WCLAP CLAP Note Echo Test | CLAP |

## Proves

Which dialect the host sends notes and raw MIDI in when a plug-in's note input lists only one (MIDI, MIDI 2 or CLAP),
that it passes sysex through and sets the event flags it means to, and that it receives the note and MIDI events a
plug-in sends out.

## A correct host

**Loads** all three plug-ins and reads the echoed events back from the output event list.

Each port lists one supported dialect and names it as preferred
([`ext/note-ports.h`](../../external/clap/include/clap/ext/note-ports.h): `preferred_dialect` is "one value of
clap_note_dialect"). CLAP does not forbid a host from sending another dialect:
[`events.h`](../../external/clap/include/clap/events.h) says "the preferred way of sending a note event is to use
CLAP_EVENT_NOTE_*", and that plug-ins "are encouraged to be able to handle note events encoded as raw midi or midi2".
So the dialect an event arrives in is the host's choice, not a pass or fail. The plug-ins take, mark and echo whatever
arrives, and the marks show what the host actually sent. A host that translates its notes into the listed dialect shows
the values in the table below; one that sends, say, `CLAP_EVENT_NOTE_ON` to the MIDI plug-in shows `1` where the table
has `11`. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-ins' side (ports, dialects, the marks and the echo of
every event type below) on the native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Event type / flags` | 2 | Left: event type. Right: event flags. |
| Note input 0, `Events in` | | The dialect in the table above |
| Note output 0, `Echo` | | The same dialect |

There is no audio input and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` on every valid block
(one stereo output with buffers) and `CLAP_PROCESS_ERROR` otherwise.

## Output

Both channels are zero except on the frame of an input event in `CLAP_CORE_EVENT_SPACE_ID`. On that frame:

```text
left  = header.type + 1
right = header.flags
```

The `+ 1` keeps `CLAP_EVENT_NOTE_ON`, whose type is `0`, apart from a frame with no event.

If several events share a frame, the last one in the list wins. Events of other spaces, and events stamped at or past
the end of the block, are ignored: not marked and not echoed. Each plug-in marks and echoes every event type below,
whatever dialect its port lists.

| Input event (type) | Left | Echoed |
| --- | ---: | --- |
| `CLAP_EVENT_NOTE_ON` (`0`), `CLAP_EVENT_NOTE_OFF` (`1`), `CLAP_EVENT_NOTE_CHOKE` (`2`) | `1`, `2`, `3` | Yes |
| `CLAP_EVENT_MIDI` (`10`), `CLAP_EVENT_MIDI_SYSEX` (`11`), `CLAP_EVENT_MIDI2` (`12`) | `11`, `12`, `13` | Yes |
| Any other core event, such as a parameter value (`5`) | type + 1 | No |

An echoed event is pushed to the output event list unchanged (same time, flags, port and payload), in input order. The
echo keeps the input's dialect even when the output port does not list it: a `CLAP_EVENT_NOTE_ON` or
`CLAP_EVENT_NOTE_CHOKE` sent to the MIDI plug-in comes back as that CLAP event, not translated to MIDI. A
sysex echo points at the host's own buffer. [`events.h`](../../external/clap/include/clap/events.h) says that, from
plug-in to host, the buffer lives "only the duration of a try_push call", so the host copies the bytes inside
`try_push`. Values
used here are exact in 32-bit floats: `CLAP_EVENT_IS_LIVE` is `1`, `CLAP_EVENT_DONT_RECORD` is `2`.

## Validation protocol

Render one block of at least 16 frames with each plug-in and schedule these events on note port 0, channel 2, key 60,
using the host's own note and MIDI calls. All other samples are `0`.

| Frame | Host sends | `midi-echo` left | `midi-echo.midi2` left | `midi-echo.clap` left | Echoed back as |
| ---: | --- | ---: | ---: | ---: | --- |
| 1 | Note-on, marked live | `11`, right `1` | `13`, right `1` | `1`, right `1` | The translated event |
| 2 | MIDI pitch bend `0xe2 0x10 0x50` | `11` | `13` | not tested | The same bytes, or the MIDI 2 packet |
| 3 | Sysex `f0 7d 01 02 f7` | `12` | `12` or `13` | not tested | The same bytes, or the MIDI 2 packet |
| 4 | Note-off | `11` | `13` | `2` | The translated event |
| 5 | Note-choke, or note-off with key `-1` | not tested | not tested | `3` or `2` | The same CLAP note event |

Only the live note shows a non-zero right channel.

Rows marked "not tested" have no form in that plug-in's dialect, so what a host does with them is left open:

- A choke and a note-off for every key exist only as CLAP note events
  ([`events.h`](../../external/clap/include/clap/events.h): `CLAP_EVENT_NOTE_CHOKE`; key `-1` is a wildcard).
- A pitch bend and a sysex message are raw MIDI; the CLAP dialect carries `clap_event_note` and
  `clap_event_note_expression` ([`ext/note-ports.h`](../../external/clap/include/clap/ext/note-ports.h)).

For the MIDI 2 plug-in, CLAP allows sysex either as `CLAP_EVENT_MIDI_SYSEX` or as a series of `CLAP_EVENT_MIDI2`
packets, and asks for `CLAP_EVENT_MIDI_SYSEX` where possible
([`events.h`](../../external/clap/include/clap/events.h), comment above `clap_event_midi2`). Both pass at frame 3: a
host that sends a packet shows `13` there, and one that sends the sysex event shows `12`.

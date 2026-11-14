# midi-echo

A GUI-free note effect with one stereo output, one note input and one note output. It sends every note and MIDI event
it receives straight back out, and marks each one in the output with its event type and flags, so a host test sees the
dialect each event arrived in. The bundle holds three plug-ins that differ only in the dialect their note ports take.
Each takes exactly one, so there is one correct way for a host to send each event. Native CLAP and WCLAP compile the
same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

| Plug-in ID | Name | Note ports: supported and preferred dialect |
| --- | --- | --- |
| `com.charlieculbert.wclap-test-plugins.midi-echo` | WCLAP MIDI Echo Test | MIDI |
| `com.charlieculbert.wclap-test-plugins.midi-echo.midi2` | WCLAP MIDI 2 Echo Test | MIDI 2 |
| `com.charlieculbert.wclap-test-plugins.midi-echo.clap` | WCLAP CLAP Note Echo Test | CLAP |

## Proves

The host sends notes and raw MIDI to a plug-in in the one dialect its note input takes (MIDI, MIDI 2 or CLAP), passes
sysex through, sets the event flags it means to, and receives the note and MIDI events a plug-in sends out.

## A correct host

**Loads** all three plug-ins. It reads each plug-in's dialect from its note input port, translates its own notes into
that dialect where it can, and reads the echoed events back from the output event list. A host that sends a dialect the
port does not list shows it in the marks: for example `CLAP_EVENT_NOTE_ON` (`0`) where the table below expects
`CLAP_EVENT_MIDI` (`10`) or `CLAP_EVENT_MIDI2` (`12`). [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-ins' side
(ports, dialects, the marks and the echo of every event type below) on the native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Event type / flags` | 2 | Left: event type. Right: event flags. |
| Note input 0, `Events in` | | The dialect in the table above |
| Note output 0, `Echo` | | The same dialect |

There is no audio input and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Output

Both channels are zero except on the frame of an input event in `CLAP_CORE_EVENT_SPACE_ID`. On that frame:

```text
left  = header.type
right = header.flags
```

If several events share a frame, the last one in the list wins. Events of other spaces, and events stamped at or past
the end of the block, are ignored: not marked and not echoed. Each plug-in marks and echoes every event type below,
whatever dialect its port lists, so a host's mistakes show up instead of vanishing.

| Input event | Marked | Echoed |
| --- | --- | --- |
| `CLAP_EVENT_NOTE_ON` (`0`), `CLAP_EVENT_NOTE_OFF` (`1`), `CLAP_EVENT_NOTE_CHOKE` (`2`) | Yes | Yes |
| `CLAP_EVENT_MIDI` (`10`), `CLAP_EVENT_MIDI_SYSEX` (`11`), `CLAP_EVENT_MIDI2` (`12`) | Yes | Yes |
| Any other core event, such as a parameter value | Yes | No |

An echoed event is pushed to the output event list unchanged (same time, flags, port and payload), in input order. A
sysex event's buffer is the host's; the echo points at it, so the host reads it back before the block ends. Values
used here are exact in 32-bit floats: `CLAP_EVENT_IS_LIVE` is `1`, `CLAP_EVENT_DONT_RECORD` is `2`.

## Validation protocol

Render one block of at least 16 frames with each plug-in and schedule these events on note port 0, channel 2, key 60,
using the host's own note and MIDI calls. All other samples are `0`.

| Frame | Host sends | `midi-echo` left | `midi-echo.midi2` left | `midi-echo.clap` left | Echoed back as |
| ---: | --- | ---: | ---: | ---: | --- |
| 1 | Note-on, marked live | `10`, right `1` | `12`, right `1` | `0`, right `1` | The translated event |
| 2 | MIDI pitch bend `0xe2 0x10 0x50` | `10` | `12` | not tested | The same bytes, or the MIDI 2 packet |
| 3 | Sysex `f0 7d 01 02 f7` | `11` | `11` or `12` | not tested | The same bytes, or the MIDI 2 packet |
| 4 | Note-off | `10` | `12` | `1` | The translated event |
| 5 | Note-choke, or note-off with key `-1` | not tested | not tested | `2` or `1` | The same CLAP note event |

Only the live note shows a non-zero right channel.

Rows marked "not tested" have no form in that plug-in's dialect, so what a host does with them is left open:

- A choke and a note-off for every key exist only as CLAP note events
  ([`events.h`](../../external/clap/include/clap/events.h): `CLAP_EVENT_NOTE_CHOKE`; key `-1` is a wildcard).
- A pitch bend and a sysex message are raw MIDI; the CLAP dialect carries `clap_event_note` and
  `clap_event_note_expression` ([`ext/note-ports.h`](../../external/clap/include/clap/ext/note-ports.h)).

For the MIDI 2 plug-in, CLAP allows sysex either as `CLAP_EVENT_MIDI_SYSEX` or as a series of `CLAP_EVENT_MIDI2`
packets, and asks for `CLAP_EVENT_MIDI_SYSEX` where possible
([`events.h`](../../external/clap/include/clap/events.h), comment above `clap_event_midi2`). Both pass at frame 3: a
host that sends a packet shows `12` there, and one that sends the sysex event shows `11`.

# midi-echo

A GUI-free note effect with one stereo output, one note input and one note output. It sends every note and MIDI event
it receives straight back out, and marks each one in the output with its event type and flags, so a host test sees the
dialect each event arrived in. The bundle holds two plug-ins that differ only in the dialects their note ports take.
Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain
no Wasm-specific branches or APIs.

| Plug-in ID | Name | Note ports: supported dialects | Preferred |
| --- | --- | --- | --- |
| `com.charlieculbert.wclap-test-plugins.midi-echo` | WCLAP MIDI Echo Test | CLAP, MIDI | MIDI |
| `com.charlieculbert.wclap-test-plugins.midi-echo.midi2` | WCLAP MIDI 2 Echo Test | MIDI 2 | MIDI 2 |

## Proves

The host sends notes and raw MIDI to a plug-in in the dialect its note input asks for (MIDI or MIDI 2), passes sysex
through, sets the event flags it means to, and receives the note and MIDI events a plug-in sends out.

## A correct host

**Loads** both plug-ins. It picks each plug-in's dialect from its note input port, translates its own notes into that
dialect where it can, and reads the echoed events back from the output event list. A host that ignores the preferred
dialect shows `CLAP_EVENT_NOTE_ON` (`0`) where the table below expects `CLAP_EVENT_MIDI` (`10`) or `CLAP_EVENT_MIDI2`
(`13`). [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-ins' side (ports, dialects, the marks and the echo of
every event type below) on the native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Event type / flags` | 2 | Left: event type. Right: event flags. |
| Note input 0, `Events in` | | The dialects in the table above |
| Note output 0, `Echo` | | The same dialects |

There is no audio input and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Output

Both channels are zero except on the frame of an input event in `CLAP_CORE_EVENT_SPACE_ID`. On that frame:

```text
left  = header.type
right = header.flags
```

If several events share a frame, the last one in the list wins. Events of other spaces, and events stamped at or past
the end of the block, are ignored: not marked and not echoed.

| Input event | Marked | Echoed |
| --- | --- | --- |
| `CLAP_EVENT_NOTE_ON` (`0`), `CLAP_EVENT_NOTE_OFF` (`1`), `CLAP_EVENT_NOTE_CHOKE` (`2`) | Yes | Yes |
| `CLAP_EVENT_MIDI` (`10`), `CLAP_EVENT_MIDI_SYSEX` (`11`), `CLAP_EVENT_MIDI2` (`13`) | Yes | Yes |
| Any other core event, such as a parameter value | Yes | No |

An echoed event is pushed to the output event list unchanged (same time, flags, port and payload), in input order. A
sysex event's buffer is the host's; the echo points at it, so the host reads it back before the block ends. Values
used here are exact in 32-bit floats: `CLAP_EVENT_IS_LIVE` is `1`, `CLAP_EVENT_DONT_RECORD` is `2`.

## Validation protocol

Render one block of at least 16 frames with each plug-in and schedule these events on note port 0, channel 2, key 60,
using the host's own note and MIDI calls. All other samples are `0`.

| Frame | Host sends | `midi-echo` left | `midi-echo.midi2` left | Echoed back as |
| ---: | --- | ---: | ---: | --- |
| 1 | Note-on, marked live | `10`, right `1` | `13`, right `1` | The translated event |
| 2 | MIDI pitch bend `0xe2 0x10 0x50` | `10` | `13` | The same bytes, or the MIDI 2 packet |
| 3 | Sysex `f0 7d 01 02 f7` | `11` | `11` | The same bytes |
| 4 | Note-off | `10` | `13` | The translated event |
| 5 | Note-choke, or note-off with key `-1` | `2` or `1` | not tested | A CLAP note event |

A choke and a note-off for every key have no MIDI form, so a host sends them as CLAP note events (frame 5) to the
plug-in that prefers MIDI, since it also takes CLAP. The MIDI 2 plug-in does not take CLAP notes, so what a host does
with those two there is left open. Only the live note shows a non-zero right channel.

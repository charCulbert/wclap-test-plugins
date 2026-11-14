# note-ids

A GUI-free instrument with one stereo output and one CLAP note input. It writes the note ID of every note it starts and
every note it ends into the output, and sends `CLAP_EVENT_NOTE_END` for each note it ends. Native CLAP and WCLAP compile
the same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or
APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.note-ids`

## Proves

The host passes its note IDs through to the plug-in, ends notes by ID, key, or a wildcard (`-1`) that matches every
voice on a key, delivers note chokes, and receives `CLAP_EVENT_NOTE_END` with the address of the note-on.

## A correct host

**Loads** the plug-in, gives each note-on a note ID of 0 or more, and reads `NOTE_END` events back. The note IDs it
assigned appear in the output on the frames it scheduled, and every note it ends comes back as a `NOTE_END` with the
note ID, port, channel and key it gave the note-on. A host without note ID support sends `-1`, which the output shows.
[`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (ports, every row below, the `NOTE_END` events) on the
native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Started / ended note IDs` | 2 | Left: started notes. Right: ended notes. |
| Note input 0, `Notes` | | CLAP dialect only |

The plug-in has no audio input, no parameters, and no note output port. MIDI events are ignored. It returns
`CLAP_PROCESS_CONTINUE` from every block.

## Output

Both channels are zero except on the frame of a note event. An event stamped at or past the end of the block is applied
on the block's last frame. A note ID is written as:

```text
encode(note_id) = note_id + 1   if note_id >= 0
                = -1            if note_id == -1
```

The `+ 1` keeps note ID 0 visible. Encoded values are exact in 32-bit floats for note IDs up to 2^24 - 1.

| Event | Left | Right | Output event |
| --- | --- | --- | --- |
| `CLAP_EVENT_NOTE_ON` | `encode(note_id)` | | Starts a voice with the event's note ID, port, channel and key. A note-on with port, channel or key `-1` is invalid and is ignored. |
| `CLAP_EVENT_NOTE_OFF` | | `encode(note_id)` of each voice it ends | One `CLAP_EVENT_NOTE_END` per ended voice, on the same frame |
| `CLAP_EVENT_NOTE_CHOKE` | | As note-off | As note-off |

A note-off or choke ends every sounding voice it matches. It matches a voice when each of its note ID, port, channel
and key is `-1` or equal to the voice's. So a note-off with note ID `-1` and key 60 ends every voice on key 60, and one
with every field `-1` ends all voices. A note-off that matches nothing writes nothing.

Each `NOTE_END` carries the voice's own note ID, port, channel and key from its note-on, not the note-off's. When one
event ends several voices, their encoded IDs add up on the right channel and their `NOTE_END` events come out in the
order the voices started.

Several note-ons on one frame add up on the left channel too. Activating or resetting the plug-in ends every voice
without sending `NOTE_END`. There are 64 voices; a note-on with none free still writes its click but starts no voice.

## Validation protocol

Render one block of at least 32 frames from a freshly activated plug-in and schedule these events on note port 0. All
other samples are `0`.

| Frame | Event (note ID, channel, key) | Left | Right | `NOTE_END` (note ID, channel, key) |
| ---: | --- | ---: | ---: | --- |
| 2 | Note-on (0, 0, 60) | `1` | | |
| 4 | Note-on (100, 0, 60) | `101` | | |
| 6 | Note-on (200, 1, 62) | `201` | | |
| 10 | Note-off (100, 0, 60) | | `101` | (100, 0, 60) |
| 12 | Note-off (999, 1, 62) | | | none: no voice has ID 999 |
| 14 | Note-off (-1, 0, 62) | | | none: the voice on key 62 is on channel 1 |
| 16 | Note-choke (200, 1, 62) | | `201` | (200, 1, 62) |
| 18 | Note-on (300, 0, 60) | `301` | | |
| 20 | Note-off (-1, 0, 60) | | `302` | (0, 0, 60), then (300, 0, 60) |
| 22 | Note-off (-1, -1, -1) | | | none: no voices left |

A host that drops or renumbers note IDs shows other values on the left channel. A host that sends note ID `-1` on
note-on writes `-1` there. A host that rewrites the wildcard at frame 20 to a single note ID ends only that voice.

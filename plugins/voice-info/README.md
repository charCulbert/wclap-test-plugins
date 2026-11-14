# voice-info

A GUI-free instrument with one stereo output and one MIDI note input. It reports a voice count and capacity through
`voice_info`. A MIDI program change sets a new voice count, after which it calls `voice_info.changed` on the host. The
output shows the voice count and how many times the host has read the voice info. It makes no sound. Native CLAP and
WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific
branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.voice-info`

## Proves

The host reads a plug-in's voice info while it is active and reads it again when the plug-in calls
`voice_info.changed`.

## A correct host

**Loads** the plug-in. If it reads the voice info, it does so while the plug-in is active: `get` is
`[main-thread & active]` ([`ext/voice-info.h`](../../external/clap/include/clap/ext/voice-info.h)), and a host that
calls it while the plug-in is inactive breaks the CLAP contract. The first read gives 4 voices of 8.

After a program change the plug-in calls `request_callback` and then, from `on_main_thread`, `voice_info.changed`. The
header says only that `changed` "informs the host that the voice info has changed"; it does not say what the host does
next. Reading the voice info again while the plug-in is active is the expected response, and a host that does so knows
the new count. A host that ignores `changed` keeps the old count; that is a host choice, not a failure. The protocol's
program change needs a host that can send MIDI to the plug-in. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's
side (ports, each voice count's info, the output, one `request_callback` and one `changed` per change, including a
change still pending at deactivate) on the native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Voice count / voice info reads` | 2 | Left: voice count. Right: number of reads. |
| Note input 0, `Program change` | | MIDI dialect only |

The plug-in plays no notes; the note input exists for the program change, which only MIDI has. There is no audio input
and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Voice info

| Program | `voice_count` | `voice_capacity` | `flags` |
| --- | ---: | ---: | --- |
| None yet | 4 | 8 | `0` |
| 0 | 1 | 8 | `0` |
| 1 | 4 | 8 | `0` |
| Any other | 8 | 8 | `0` |

`flags` never has `CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES`: with it, the plug-in "will then rely upon the note_id to
distinguish between" overlapping notes (`voice-info.h`), and its MIDI-only port carries no note IDs.

## Output

```text
left  = voice count
right = number of voice_info.get calls since the plug-in was created
```

The right channel is a heuristic for how often the host reads the voice info; a correct host may read it any number of
times. Both channels are written on every frame. An event stamped at or past the end of the block is applied on the block's
last frame.

| Event | Effect |
| --- | --- |
| MIDI program change (`0xCn p`), any channel | Sets the voice count from the table above, from the event's frame on. If the count changes, the plug-in calls `host.request_callback` and then, in `on_main_thread`, `voice_info.changed`. |
| Any other event | Ignored |

The voice count survives reactivation and `reset`. `voice_info.get` reports the new count as soon as it changes, before
`changed` is called. `changed` is `[main-thread]` with no active requirement, so if the plug-in is deactivated before
the main-thread callback runs, it still calls `changed` from that callback. A host that wants the new count then reads
it after the next `activate`.

## Validation protocol

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load and activate; read the voice info | 4 of 8, flags `0` |
| 2 | Send MIDI `0xC0 0` at frame 8 of a block (the host must be able to send MIDI) | Left `4` before frame 8, `1` from frame 8 |
| 3 | Service the main-thread callback while the plug-in is active | The plug-in calls `voice_info.changed` once |
| 4 | Read the voice info again (or let the host's own `changed` handling do it), then render a block | 1 of 8, flags `0` |

The read count on the right channel is a diagnostic, not a pass/fail check: a host that reads the voice info after
steps 1 and 4 shows it rise each time.

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

**Loads** the plug-in and reads its voice info after activating it: 4 voices of 8, overlapping notes supported. After a
program change it services the plug-in's `request_callback`, receives `voice_info.changed` from `on_main_thread`, and
reads the voice info again, so the host now knows the new count. A host that reads the voice info once and ignores
`changed` keeps the old count. A host that calls `voice_info.get` while the plug-in is inactive breaks the CLAP
contract: `get` is `[main-thread & active]` ([`ext/voice-info.h`](../../external/clap/include/clap/ext/voice-info.h)).
The protocol's program change needs a host that can send MIDI to the plug-in. [`tests/smoke.cpp`](tests/smoke.cpp) checks
the plug-in's side (ports, each voice count's info, the output, one `request_callback` and one `changed` per change, no
`changed` while inactive) on the native build under `ctest`.

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
| None yet | 4 | 8 | `CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES` |
| 0 | 1 | 8 | `0` |
| 1 | 4 | 8 | `CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES` |
| Any other | 8 | 8 | `CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES` |

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
`changed` is called. The plug-in calls `changed` only while it is active, since that is when the host may call `get`. If
it is deactivated before the main-thread callback runs, it drops the pending `changed`; the host reads the voice info
afresh after the next `activate`.

## Validation protocol

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load and activate; read the voice info | 4 of 8, overlapping |
| 2 | Send MIDI `0xC0 0` at frame 8 of a block (the host must be able to send MIDI) | Left `4` before frame 8, `1` from frame 8 |
| 3 | Service the main-thread callback while the plug-in is active | The plug-in calls `voice_info.changed` once |
| 4 | Read the voice info again (or let the host's own `changed` handling do it), then render a block | 1 of 8, no overlap |

The read count on the right channel is a diagnostic, not a pass/fail check: a host that reads the voice info after
steps 1 and 4 shows it rise each time.

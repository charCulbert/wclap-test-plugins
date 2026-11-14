# voice-info

A GUI-free instrument with one stereo output and one note input. It reports a voice count and capacity through
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
reads the voice info again, so the right channel goes up by one and the host now knows the new count. A host that reads
the voice info once and ignores `changed` keeps the old count, and its right channel stays the same. A host that calls
`voice_info.get` while the plug-in is inactive breaks the CLAP contract. [`tests/smoke.cpp`](tests/smoke.cpp) checks
the plug-in's side (ports, each voice count's info, the output, one `request_callback` and one `changed` per change) on
the native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Voice count / voice info reads` | 2 | Left: voice count. Right: number of reads. |
| Note input 0, `Program change` | | CLAP and MIDI dialects; CLAP preferred |

There is no audio input and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

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

Both channels are written on every frame. An event stamped at or past the end of the block is applied on the block's
last frame.

| Event | Effect |
| --- | --- |
| MIDI program change (`0xCn p`), any channel | Sets the voice count from the table above, from the event's frame on. If the count changes, the plug-in calls `host.request_callback` and then, in `on_main_thread`, `voice_info.changed`. |
| Any other event | Ignored |

The voice count survives reactivation and `reset`. `voice_info.get` reports the new count as soon as it changes, before
`changed` is called.

## Validation protocol

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load and activate; read the voice info | 4 of 8, overlapping; right channel `n >= 1` |
| 2 | Send MIDI `0xC0 0` at frame 8 of a block | Left `4` before frame 8, `1` from frame 8 |
| 3 | Service the main-thread callback | The plug-in calls `voice_info.changed` once |
| 4 | Read the voice info again (or let the host's own `changed` handling do it), then render a block | 1 of 8, no overlap; right channel `n + 1` or more |

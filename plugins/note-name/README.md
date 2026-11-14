# note-name

A GUI-free instrument with one stereo output and one note input. It names four drum keys and gives one name to every
other key. A MIDI program change switches it to a second kit that renames one key, after which it calls
`note_name.changed` on the host. The output shows the kit and how many times the host has read the names. Native CLAP
and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no
Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.note-name`

## Proves

The host reads a plug-in's note names, including a name for any key (`-1`), and reads them again when the plug-in calls
`note_name.changed`.

## A correct host

**Loads** the plug-in and shows its note names. After a program change it services the plug-in's
`request_callback`, receives `note_name.changed` from `on_main_thread`, and reads the names again: key 38 is then
`Rim`, and the right channel goes up by one. A host that caches the names and ignores `changed` keeps showing `Snare`,
and its right channel stays the same. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (ports, both
kits' names, the kit and read count in the output, one `request_callback` and one `changed` per kit change) on the
native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Kit / note name reads` | 2 | Left: kit. Right: number of reads. |
| Note input 0, `Named notes` | | CLAP and MIDI dialects; CLAP preferred |

There is no audio input and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Note names

Every name has port 0 and channel `-1` (all channels). `note_name.count` returns 5.

| Index | Key | Kit 0 | Kit 1 |
| ---: | ---: | --- | --- |
| 0 | 36 | `Kick` | `Kick` |
| 1 | 38 | `Snare` | `Rim` |
| 2 | 42 | `Closed hi-hat` | `Closed hi-hat` |
| 3 | 46 | `Open hi-hat` | `Open hi-hat` |
| 4 | `-1` | `Any key` | `Any key` |

## Output

```text
left  = kit (0 or 1)
right = number of note_name.get calls for index 0 since the plug-in was created
```

A host reading the list starts at index 0, so the right channel counts the host's reads. Both channels are written on
every frame. The plug-in starts on kit 0.

| Event | Effect |
| --- | --- |
| MIDI program change (`0xCn p`), any channel | Selects kit 0 if `p` is 0, kit 1 otherwise, from the event's frame on. If the kit changes, the plug-in calls `host.request_callback` and then, in `on_main_thread`, `note_name.changed`. |
| Any other event | Ignored |

An event stamped at or past the end of the block is applied on the block's last frame. The kit survives reactivation
and `reset`. Note names change as soon as the kit does, before `changed` is called.

## Validation protocol

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load and activate; read the note names | The kit 0 names; right channel `n >= 1` |
| 2 | Send MIDI `0xC0 1` at frame 8 of a block | Left `0` before frame 8, `1` from frame 8 |
| 3 | Service the main-thread callback | The plug-in calls `note_name.changed` once |
| 4 | Read the names again (or let the host's own `changed` handling do it), then render a block | Key 38 reads `Rim`; right channel `n + 1` or more |
| 5 | Send MIDI `0xC0 0` | Left `0`, another `changed`, key 38 reads `Snare` |

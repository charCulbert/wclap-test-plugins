# remote-controls

A GUI-free instrument with one stereo output, one note input and four parameters. It maps its parameters to two
remote-control pages. A MIDI program change reverses the mapping on the first page, after which it calls
`remote_controls.changed` and `remote_controls.suggest_page` on the host. The output shows which parameter the host set
last and how many times it has read the pages. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and
[`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.remote-controls`

## Proves

The host reads a plug-in's remote-control pages, maps each control to the parameter the page names, and reads the pages
again when the plug-in calls `remote_controls.changed`.

## A correct host

**Loads** the plug-in and maps its controls to the pages below. Turning control 1 of page `Main` sends a
`CLAP_EVENT_PARAM_VALUE` for parameter `B` (ID 1), and the left channel reads `2`. After a program change it services
the plug-in's `request_callback`, receives `changed` and `suggest_page` from `on_main_thread`, and reads the pages
again: the right channel goes up by one, and control 1 of `Main` now sets parameter `C` (ID 2), so the left channel
reads `3`.
A host that keeps its first mapping still sets `B`. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side
(ports, parameters, both layouts' pages, the output, one `request_callback`, `changed` and `suggest_page` per layout
change) on the native build under `ctest`.

## Ports and parameters

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Last parameter / page reads` | 2 | Left: last parameter set. Right: number of page reads. |
| Note input 0, `Program change` | | CLAP and MIDI dialects; CLAP preferred |

Four parameters, `A`, `B`, `C` and `D`, with IDs 0 to 3: range 0 to 1, default 0, automatable. There is no audio input.
The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Pages

Both pages are in section `Controls`. `remote_controls.count` returns 2. Unused slots hold `CLAP_INVALID_ID`.

| Index | Page ID | Name | `is_for_preset` | Layout 0, controls 0 to 3 | Layout 1, controls 0 to 3 |
| ---: | ---: | --- | --- | --- | --- |
| 0 | 1 | `Main` | `false` | `A`, `B`, `C`, `D` (IDs 0, 1, 2, 3) | `D`, `C`, `B`, `A` (IDs 3, 2, 1, 0) |
| 1 | 2 | `Preset` | `true` | `C`, `D` (IDs 2, 3) | `C`, `D` (IDs 2, 3) |

## Output

```text
left  = parameter ID + 1 of the last CLAP_EVENT_PARAM_VALUE received, 0 before the first
right = number of remote_controls.get calls for index 0 since the plug-in was created
```

A host reading the pages starts at index 0, so the right channel counts the host's reads. Both channels are written on
every frame. An event stamped at or past the end of the block is applied on the block's last frame.

| Event | Effect |
| --- | --- |
| `CLAP_EVENT_PARAM_VALUE`, parameter 0 to 3 | Sets the parameter (also read back by `params.get_value`) and the left channel, from the event's frame on |
| MIDI program change (`0xCn p`), any channel | Selects layout 0 if `p` is 0, layout 1 otherwise. If the layout changes, the plug-in calls `host.request_callback`, then in `on_main_thread` calls `remote_controls.changed` and `suggest_page`: page 2 for layout 1, page 1 for layout 0. |
| Any other event | Ignored |

`params.flush` applies parameter values the same way. The plug-in starts on layout 0; the layout survives reactivation
and `reset`. Pages change as soon as the layout does, before `changed` is called.

## Validation protocol

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load and activate; read the pages | Layout 0 pages; right channel `n >= 1` |
| 2 | Turn control 1 of page `Main` | Left `2` from that frame on |
| 3 | Send MIDI `0xC0 1` | One `request_callback`; then `changed` and `suggest_page(2)` from `on_main_thread` |
| 4 | Read the pages again (or let the host's own `changed` handling do it), then render a block | Layout 1 pages; right channel `n + 1` or more |
| 5 | Turn control 1 of page `Main` | Left `3` |

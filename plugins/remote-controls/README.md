# remote-controls

A GUI-free instrument with one stereo output, one MIDI note input and four parameters. It maps its parameters to two
remote-control pages. A MIDI program change reverses the mapping on the first page, after which it calls
`remote_controls.changed` and `remote_controls.suggest_page` on the host. The output shows which parameter the host set
last and how many times it has read the pages. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and
[`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.remote-controls`

## Proves

The host reads a plug-in's remote-control pages, maps each control to the parameter the page names, and reads the pages
again when the plug-in calls `remote_controls.changed`.

## A correct host

**Loads** the plug-in. Remote controls are optional for a host: one that does not read the pages or offer controls is
still correct, and simply shows nothing here. A host that maps its controls to the pages below sends a
`CLAP_EVENT_PARAM_VALUE` for parameter `B` (ID 1) when the control at index 1 of page `Main` is turned, and the left
channel reads `2`.

After a program change the plug-in calls `request_callback` and then, from `on_main_thread`, `changed` and
`suggest_page`. [`ext/remote-controls.h`](../../external/clap/include/clap/ext/remote-controls.h) says only that
`changed` informs the host that the pages have changed; it does not say what the host does next. Reading the pages
again is the expected response: the control at index 1 of `Main` then sets parameter `C` (ID 2), so the left channel
reads `3`. A host that keeps its first mapping still sets `B`; that is its own choice. What a host does with
`suggest_page` is up to it too: the call is a suggestion, and a host that does not switch pages is still correct. The
protocol's program change needs a host that can send MIDI to the plug-in. [`tests/smoke.cpp`](tests/smoke.cpp) checks
the plug-in's side
(ports, parameters, both layouts' pages, the output, one `request_callback`, `changed` and `suggest_page` per layout
change, `params.flush`, the draft host extension) on the native build under `ctest`.

## Ports and parameters

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Last parameter / page reads` | 2 | Left: last parameter set. Right: number of page reads. |
| Note input 0, `Program change` | | MIDI dialect only |

Four parameters, `A`, `B`, `C` and `D`, with IDs 0 to 3: range 0 to 1, default 0, automatable. There is no audio input.
The plug-in plays no notes; the note input exists for the program change, which
only MIDI has. The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

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

The right channel is a heuristic: a host that reads all the pages from the start makes one index-0 call per read. CLAP
does not fix the order or the subset a host reads, so a host that reads in another order, or only some pages, can show
a different count and still be correct. Both channels are written on every frame. An event stamped at or past the end of the block is applied on the block's last frame.

| Event | Effect |
| --- | --- |
| `CLAP_EVENT_PARAM_VALUE`, parameter 0 to 3 | Sets the parameter (also read back by `params.get_value`) and the left channel, from the event's frame on |
| MIDI program change (`0xCn p`), any channel | Selects layout 0 if `p` is 0, layout 1 otherwise. If the layout changes, the plug-in calls `host.request_callback`, then in `on_main_thread` calls `remote_controls.changed` and `suggest_page`: page 2 for layout 1, page 1 for layout 0. |
| Any other event | Ignored |

`params.flush` applies parameter values the same way and ignores any other event, including a program change: flush
is for parameter events ([`ext/params.h`](../../external/clap/include/clap/ext/params.h), `flush`). The plug-in starts on layout 0; the layout survives reactivation
and `reset`. Pages change as soon as the layout does, before `changed` is called.

The plug-in looks up the host's `clap.remote-controls` extension, and falls back to the draft ID
`clap.remote-controls.draft/2` (`CLAP_EXT_REMOTE_CONTROLS_COMPAT`) that older hosts offer.

## Validation protocol

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load and activate; read the pages | Layout 0 pages |
| 2 | Turn the control at index 1 of page `Main` | Left `2` from that frame on |
| 3 | Send MIDI `0xC0 1` (the host must be able to send MIDI) | One `request_callback`; then `changed` and `suggest_page(2)` from `on_main_thread` |
| 4 | Read the pages again (or let the host's own `changed` handling do it), then render a block | Layout 1 pages |
| 5 | Turn the control at index 1 of page `Main` | Left `3` |

The read count on the right channel is a diagnostic, not a pass/fail check: a host that reads the pages from index 0
shows it rise after steps 1 and 4. Whether the host shows page `Preset` after `suggest_page(2)` is its own choice.

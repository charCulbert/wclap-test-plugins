# param-indication

A GUI-free instrument with two stereo outputs, no inputs and one parameter. It writes the last mapping and automation
indication the host set for its parameter, with their colours, into its outputs, so a host test can read what the host
told the plug-in from the rendered audio alone. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and
[`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.param-indication`

## Proves

The host tells a plug-in, through `param_indication`, when a parameter is mapped to a control and what state its
automation is in, with the colours it shows them in, and clears both when they end.

## A correct host

**Loads** the plug-in. Showing mappings and automation is the host's choice, and a host that shows neither, or has no
`param_indication`, leaves every channel at `-1` and is still correct. A host that shows mappings or automation tells
the plug-in through `param_indication`
([`ext/param-indication.h`](../../external/clap/include/clap/ext/param-indication.h)). When it maps the parameter to a
control it calls `set_mapping` with `has_mapping` true, and output 0 reads `1` and the colour. When the parameter's
automation is present, playing, recording or overriding it calls `set_automation` with that state, and output 1 reads
it. When the mapping or automation goes away it calls them again with `false` or
`CLAP_PARAM_INDICATION_AUTOMATION_NONE`. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (ports, parameter, every channel after each call,
with a colour, with none, and with a transparent one) on the native build under `ctest`.

## Ports and parameter

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Mapping / colour` | 2 | Left: mapping. Right: mapping colour. |
| Audio output 1, `Automation state / colour` | 2 | Left: automation state. Right: automation colour. |

One parameter, `Target`: ID `1`, range 0 to 1, default 0, automatable. There is no audio input and no note port. The
plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Output

| Channel | Value |
| --- | --- |
| Output 0 left | `-1` before any `set_mapping`, then `1` if `has_mapping`, `0` if not |
| Output 0 right | Colour of the last `set_mapping` |
| Output 1 left | `-1` before any `set_automation`, then its `automation_state` (`0` none, `1` present, `2` playing, `3` recording, `4` overriding) |
| Output 1 right | Colour of the last `set_automation` |

A colour is written as `red * 65536 + green * 256 + blue`, for example `16744448` (`0xff8000`) for orange, or `-1`
when the host passes no colour. A colour with alpha 0, such as `CLAP_COLOR_TRANSPARENT`
([`color.h`](../../external/clap/include/clap/color.h)), shows nothing, so it also reads `-1`. Other alpha values are
not shown. Values are exact in 32-bit floats. Every frame of a block holds
the same values, and both channels of each output have their `constant_mask` bits set. Values are read at the start
of each block, and survive reactivation and `reset`.

The plug-in ignores the label and description `set_mapping` passes.

## Validation protocol

| Step | Host action | Output 0 | Output 1 |
| ---: | --- | --- | --- |
| 1 | Load and activate | `-1, -1` | `-1, -1` |
| 2 | Map `Target` to a control | `1`, the mapping colour | |
| 3 | Write automation for `Target` and play it | | `2`, the automation colour |
| 4 | Record automation for `Target` | | `3` |
| 5 | Remove the mapping and the automation | `0`, `-1` or a colour | `0` |

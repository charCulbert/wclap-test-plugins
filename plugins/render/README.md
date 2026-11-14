# render

A GUI-free instrument with one mono output and no inputs. It writes the render mode the host last set through the
`clap.render` extension into every sample, so a host test can tell from the rendered audio alone whether the host
switched the plug-in to offline rendering. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and
[`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.render`

## Proves

The host calls `render.set` with `CLAP_RENDER_OFFLINE` before it renders faster or slower than real time, and with
`CLAP_RENDER_REALTIME` when it goes back to playing in real time.

## A correct host

**Loads** the plug-in. The protocol below applies to a host that supports `clap.render`. CLAP does not require a host
to call `render.set`; one that never does renders `1` everywhere and is still correct. A host that does call it sets
`CLAP_RENDER_OFFLINE` before an offline render (an export, or any render not tied to an audio device's clock), and the
output reads `2`. When it plays in real time again it sets `CLAP_RENDER_REALTIME`, and the output reads `1`.
[`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (ports, `has_hard_realtime_requirement`, both modes, the
mode kept across reactivation, an unknown mode refused) on the native build under `ctest`.

## Ports and extension

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Render mode` | 1 | Render mode plus one |

There is no audio input, no note port and there are no parameters. `render.has_hard_realtime_requirement` returns
`false`, so the plug-in accepts both modes. `render.set` returns `true` for `CLAP_RENDER_REALTIME` and
`CLAP_RENDER_OFFLINE`, and `false` for any other value, which leaves the mode as it was
([`ext/render.h`](../../external/clap/include/clap/ext/render.h): `set` returns whether the mode could be applied). The
plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Output

```text
output = mode + 1
```

| Last `render.set` | Output |
| --- | ---: |
| None yet | `1` |
| `CLAP_RENDER_REALTIME` (`0`) | `1` |
| `CLAP_RENDER_OFFLINE` (`1`) | `2` |

Every frame of a block holds the same value, and the output's `constant_mask` is set. The mode is read at the start of
each block. It survives deactivation, reactivation and `reset`; only `render.set` changes it.

## Validation protocol

For a host that supports `clap.render`:

| Step | Host action | Expected output |
| ---: | --- | --- |
| 1 | Activate and play in real time | `1` |
| 2 | Start an offline render | `2` for every rendered frame |
| 3 | Finish the render and play in real time again | `1` |

A host that switches mode only after the first offline block shows `1` at the start of step 2.

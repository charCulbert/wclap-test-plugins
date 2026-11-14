# state-context

A GUI-free instrument with one stereo output and no inputs. Its saved state records which call saved it: `state.save`,
or `state_context.save` with a preset, duplicate or project context. After a load the output shows the context the
state was saved for and the context it was loaded with. Native CLAP and WCLAP compile the same
[`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.state-context`

## Proves

Which call and context the host uses to save and load a plug-in's state: `state_context` with
`CLAP_STATE_CONTEXT_FOR_PRESET` for a preset, `CLAP_STATE_CONTEXT_FOR_DUPLICATE` when it copies the plug-in, and
`CLAP_STATE_CONTEXT_FOR_PROJECT` for a project, or plain `state`.

## A correct host

**Loads** the plug-in and saves and loads its state. Which call it uses is its own choice, and the output shows the
choice. Using `state_context` is not required: [`ext/state-context.h`](../../external/clap/include/clap/ext/state-context.h)
says "if in doubt, fallback to clap_plugin_state", and "it is unspecified which context is equivalent to
clap_plugin_state.{save,load}()". So every reading below is from a correct host; they differ in how much context it
passes on.

- A host that uses `state_context` with the matching context shows `3, 3` after it saves a project and opens it
  again, `2, 2` in a duplicate's copy, and `1, 1` after it saves a preset and loads it.
- A host that uses `state.save` and `state.load` shows `0` in place of the context.
- A host that saves through one call and loads through another shows two different values, for example `3, 0` for a
  project saved with `state_context` and loaded with `state.load`. The header lists such mixes as equivalent. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's
side (ports, every save and load pair, unknown contexts, rejected states, short reads and writes) on the native build
under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Saved for / loaded with` | 2 | Left: context the state was saved for. Right: context it was loaded with. |

There is no audio input, no note port and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from
every block.

## Output

| Value | Meaning |
| ---: | --- |
| `-1` | Nothing loaded since the plug-in was created |
| `0` | `state.save` or `state.load`, which have no context, or a context this plug-in does not know |
| `1` | `CLAP_STATE_CONTEXT_FOR_PRESET` |
| `2` | `CLAP_STATE_CONTEXT_FOR_DUPLICATE` |
| `3` | `CLAP_STATE_CONTEXT_FOR_PROJECT` |

```text
left  = context of the save that wrote the last state loaded
right = context of the last successful load
```

Every frame of a block holds the same values and both channels' `constant_mask` bits are set. Values are read at the
start of each block. A failed load changes neither channel.

## State

The state is 12 bytes, three little-endian `uint32_t`: magic `0x57545343` (bytes `43 53 54 57`), version `1`, and the
context it was saved for (`0` to `3`). A load fails on a wrong magic or version, a saved-for value other than `0` to
`3`, a short state, or bytes after the 12. A save or load with a context other than 1, 2 and 3, such as one a later
CLAP version adds, works as `state.save` or `state.load` does and is recorded as `0`. Saving never depends on what was
loaded, so the same call always writes the same bytes.

## Validation protocol

| Step | Host action | With `state_context` | With `state` only |
| ---: | --- | --- | --- |
| 1 | Load the plug-in | `-1, -1` | `-1, -1` |
| 2 | Save a preset, then load it | `1, 1` | `0, 0` |
| 3 | Duplicate the plug-in; read the copy | `2, 2` | `0, 0` |
| 4 | Save the project, close it and open it again | `3, 3` | `0, 0` |

Pass/fail is only that each load succeeds and the output changes from `-1, -1`; the values record which calls and
contexts the host chose. A step a host does not offer (it has no presets, say) is skipped.

# state-context

A GUI-free instrument with one stereo output and no inputs. Its saved state records which call saved it: `state.save`,
or `state_context.save` with a preset, duplicate or project context. After a load the output shows the context the
state was saved for and the context it was loaded with. Native CLAP and WCLAP compile the same
[`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.state-context`

## Proves

The host saves and loads a plug-in's state through `state_context` with the context that matches what it is doing:
`CLAP_STATE_CONTEXT_FOR_PRESET` for a preset, `CLAP_STATE_CONTEXT_FOR_DUPLICATE` when it copies the plug-in, and
`CLAP_STATE_CONTEXT_FOR_PROJECT` for a project.

## A correct host

**Loads** the plug-in. When it saves a project and opens it again, the output reads `3, 3`. When it duplicates the
plug-in, the copy reads `2, 2`. When it saves a preset and loads it, the output reads `1, 1`. A host that ignores
`state_context` and uses `state.save` and `state.load` shows `0` in place of the context. A host that saves for one
purpose and loads for another shows two different values. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's
side (ports, every save and load pair, rejected contexts and states, short reads and writes) on the native build
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
| `0` | `state.save` or `state.load`, which have no context |
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

The state is 12 bytes, three little-endian `uint32_t`: magic `0x57545343`, version `1`, and the context it was saved
for. A load fails on a wrong magic or version, an unknown saved-for value, a short state, or bytes after the 12. Save
and load fail for any context other than 1, 2 and 3. Saving never depends on what was loaded, so the same call always
writes the same bytes.

## Validation protocol

| Step | Host action | Expected output |
| ---: | --- | --- |
| 1 | Load the plug-in | `-1, -1` |
| 2 | Save a preset, then load it | `1, 1` |
| 3 | Duplicate the plug-in; read the copy | `2, 2` |
| 4 | Save the project, close it and open it again | `3, 3` |

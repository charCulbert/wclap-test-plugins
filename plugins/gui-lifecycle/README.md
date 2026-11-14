# gui-lifecycle

A stereo pass-through with an embedded WebView interface.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.gui-lifecycle`

## Proves

The host opens an embedded WebView interface in CLAP order (`create`, `get_size`, `set_parent`, `show`) and serves the
plug-in's multi-file resources to it.

## A correct host

**Loads** the plug-in and shows its interface. The page reads `Host accepted.` once the plug-in has answered its ready
message.

Specifically, the host:

1. Offers `clap.webview` and calls `gui.create("webview", false)`. Floating windows (windows the plug-in owns) and other
   window APIs are refused.
2. Calls `gui.get_size` before `gui.set_parent`. `get_size` reports 400 x 260. `set_parent` succeeds only after
   `get_size`. Pass a `webview` window with a null `ptr`.
3. Calls `gui.show` after a successful `set_parent`. `show` fails before that.
4. Loads `webview.get_uri` (`/ui/index.html`) and resolves the page's relative links through `webview.get_resource`:
   `/ui/style.css`, `/ui/main.js` and `/ui/message.txt`. The page waits for the stylesheet and `message.txt` before
   it sends its ready message.
5. Delivers the page's message to `webview.receive`, and the plug-in's reply back to the page. The plug-in takes
   messages from `set_parent` until `gui.destroy`, shown or hidden, since a page can load and send before `show`.
   Messages before `set_parent` or after `destroy` are rejected.

A host that calls these out of order sees a call fail; the plug-in neither crashes nor quietly recovers.
[`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (call order, size, resources through a stream that
takes a few bytes per write, messages) on the native build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio input 0, `Stereo Input` | 2 | Passed through to the output |
| Audio output 0, `Stereo Output` | 2 | The input, unchanged; in-place pair with the input |

There are no note ports and no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from every block.

## Resources

`get_resource` serves files from the plug-in's `resources/` directory: the WCLAP archive root, `Contents/Resources` in
a macOS bundle, or the directory of the `.clap` file on Linux and Windows (the entry path is the DSO there,
[`entry.h`](../../external/clap/include/clap/entry.h)). It writes each file in a loop until the host's stream has taken
every byte, and fails if a write returns a negative value or takes nothing
([`stream.h`](../../external/clap/include/clap/stream.h)). Media types: `text/html` for `.html`, `text/css`,
`text/javascript` for `.js`, `text/plain` for `.txt`, and `application/octet-stream` for anything else. Paths
containing `..` are refused.

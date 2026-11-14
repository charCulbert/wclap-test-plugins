# gui-lifecycle

A stereo pass-through with an embedded WebView interface.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.gui-lifecycle`

## Proves

The host opens an embedded WebView interface through `clap.webview/3`, serves the plug-in's multi-file resources to it,
and carries messages between the page and the plug-in.

## A correct host

**Loads** the plug-in and shows its interface. The page reads `Host accepted.` once the plug-in has answered its ready
message.

`clap.webview/3` is a draft extension ([`ext/draft/webview.h`](../../external/clap/include/clap/ext/draft/webview.h)),
so a host that does not offer it is still correct; it just cannot show this interface. The plug-in exposes no other
draft extension.

The host:

1. Offers `clap.webview` and calls `gui.create("webview", false)`. Floating windows (windows the plug-in owns) and other
   window APIs are refused. A second `create` while the GUI exists is refused.
2. Calls `webview.get_uri` (`/ui/index.html`) and resolves the page's relative links through `webview.get_resource`:
   `/ui/style.css`, `/ui/main.js` and `/ui/message.txt`. The page waits for the stylesheet and `message.txt` before
   it sends its ready message.
3. Delivers the page's message to `webview.receive`, and the plug-in's reply back to the page. `webview.h` says
   `get_uri` "must be called at least once before any messages are sent (or accepted) by the host", so the plug-in takes
   messages once the GUI is created and `get_uri` has been called, until `gui.destroy`, whether or not the view is
   parented or shown. The page sends its ready message again every 250 ms until a reply arrives, so a message the
   host delivers too early is not lost.

[`gui.h`](../../external/clap/include/clap/ext/gui.h) describes the order for an embedded window: `create`, then
`get_size` (400 x 260 here) or `set_size`, then `set_parent`, then `show`; after that `hide` and `show` as often as the
host likes, and `destroy` when done. The plug-in does not require `get_size` before `set_parent`. `set_parent` takes a
`webview` window with a null `ptr`. `show` succeeds only after a successful `set_parent`. The GUI is not resizable, so
`set_size` is refused.

A call the plug-in cannot take in its current state (`show` before `set_parent`, any GUI call before `create`,
`set_size`, a message before `create` and `get_uri`) is refused: it returns `false` and is logged through the host's
`clap.log` at `CLAP_LOG_HOST_MISBEHAVING`. The plug-in keeps running. If the host destroys the plug-in with its GUI
still created, the plug-in logs that, destroys the GUI and carries on.

[`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (call order, refusals and their log lines, size,
resources through a stream that takes a few bytes per write, media types, messages and replies, repeated entry
`init`/`deinit`) on the native build under `ctest`.

## Messages

Each message is an `ArrayBuffer` of bytes. The page sends one byte, a command; the plug-in replies with two bytes,
`{command, accepted}`. The only command is `1` (ready), answered with `{1, 1}`; any other command is answered with
`{command, 0}`. The page shows `Host accepted.` on `{1, 1}` and `Host rejected.` on a reply with `accepted` 0. If the
host sends the page a one-byte message, the page sends it back to the plug-in.

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
`text/javascript` for `.js`, `text/plain` for `.txt`, and `application/octet-stream` for anything else. `webview.h`
says the type is written "if the capacity is large enough", and the call returns true only "if the media type and
entire contents of the resource were provided". So if `mime_capacity` is too small, the plug-in writes no type, still
streams the file, and returns `false`. Paths containing `..` are refused.

The entry path comes from `clap_entry.init`. [`entry.h`](../../external/clap/include/clap/entry.h) allows `init` and
`deinit` to be called more than once in matched pairs, so the plug-in counts them under a mutex and forgets the path
only on the last `deinit`.

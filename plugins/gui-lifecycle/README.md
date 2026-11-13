# gui-lifecycle

A stereo pass-through with an embedded WebView interface.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.gui-lifecycle`

## Proves

The host opens an embedded WebView interface in CLAP order (`create`, `get_size`, `set_parent`, `show`) and serves the
plug-in's multi-file resources to it.

## A correct host

**Loads** the plug-in and shows its interface. The page reads `Host accepted.` once it is visible.

Specifically, the host:

1. Offers `clap.webview` and calls `gui.create("webview", false)`. Floating windows (windows the plug-in owns) and other
   window APIs are refused.
2. Calls `gui.get_size` before `gui.set_parent`. `set_parent` succeeds only after `get_size`. Pass a `webview` window
   with a null `ptr`.
3. Calls `gui.show` after a successful `set_parent`. `show` fails before that.
4. Loads `webview.get_uri` (`/ui/index.html`) and resolves `style.css`, `main.js` and `message.txt` through
   `webview.get_resource`. The page itself waits for the stylesheet and `message.txt` before it sends its ready message.
5. Delivers the page's message to `webview.receive` while the interface is visible, and the plug-in's reply back to the
   page. Messages are rejected before `show` or after `hide`.

A host that calls these out of order sees a call fail; the plug-in neither crashes nor quietly recovers. Audio passes
through unchanged on the main stereo port.

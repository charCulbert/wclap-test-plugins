# log

A GUI-free instrument with one stereo output and no inputs. It sends one message at every `clap_log_severity` through
the host's `log` extension: the first from the audio thread in its first `process()` call, the rest from the main thread
in the `on_main_thread` call that follows. The output shows how many messages it has sent and the severity of the last.
It makes no sound. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and [`Entry.cpp`](Entry.cpp); the
sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.log`

## Proves

The host takes `log` calls from the audio thread and the main thread, at every severity, with UTF-8 text, and carries
on running the plug-in.

## A correct host

**Loads** the plug-in, provides `clap.log`, and records or shows all seven messages below with their severities and
exact text, including the two non-ASCII characters. It keeps processing the plug-in afterwards: a `FATAL`,
`HOST_MISBEHAVING` or `PLUGIN_MISBEHAVING` message is a report, not a request to stop. A host that does not provide
`clap.log` gets no messages and the left channel stays `0`. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's
side (ports, every message in order, the output before and after each step, one `request_callback`) on the native
build under `ctest`.

## Ports

| Port | Channels | Use |
| --- | --- | --- |
| Audio output 0, `Messages sent / last severity` | 2 | Left: messages sent. Right: severity of the last message. |

There is no audio input, no note port and there are no parameters. The plug-in returns `CLAP_PROCESS_CONTINUE` from
every block.

## Messages

| # | Thread | Severity | Text |
| ---: | --- | --- | --- |
| 1 | Audio, first `process()` | `CLAP_LOG_DEBUG` (`0`) | `log test 1/7: debug, audio thread` |
| 2 | Main, `on_main_thread` | `CLAP_LOG_INFO` (`1`) | `log test 2/7: info, main thread, UTF-8 é✓` |
| 3 | Main | `CLAP_LOG_WARNING` (`2`) | `log test 3/7: warning, main thread` |
| 4 | Main | `CLAP_LOG_ERROR` (`3`) | `log test 4/7: error, main thread` |
| 5 | Main | `CLAP_LOG_FATAL` (`4`) | `log test 5/7: fatal, main thread` |
| 6 | Main | `CLAP_LOG_HOST_MISBEHAVING` (`5`) | `log test 6/7: host misbehaving, main thread` |
| 7 | Main | `CLAP_LOG_PLUGIN_MISBEHAVING` (`6`) | `log test 7/7: plug-in misbehaving, main thread` |

Message 2 ends in U+00E9 and U+2713, the UTF-8 bytes `c3 a9 e2 9c 93`. In its first `process()` call, after message 1,
the plug-in calls `host.request_callback`. Messages 2 to 7 are sent in the first `on_main_thread` call after that, in
order. Each message is sent once per plug-in instance; reactivating does not send them again.

## Output

```text
left  = number of messages sent so far
right = severity of the last message sent, -1 before the first
```

Every frame of a block holds the same values, and both channels' `constant_mask` bits are set. Values are read after
the block's own message, so the first block already shows `1, 0`.

## Validation protocol

| Step | Host action | Expected |
| ---: | --- | --- |
| 1 | Load, activate and render one block | Message 1 logged; output `1, 0` |
| 2 | Service the main-thread callback | Messages 2 to 7 logged, in order |
| 3 | Render another block | Output `7, 6`; the host is still processing the plug-in |

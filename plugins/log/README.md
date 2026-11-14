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

**Loads** the plug-in. If it provides `clap.log`, it takes all seven calls below without crashing or blocking, from
the audio thread and the main thread: `log` is `[thread-safe]`
([`ext/log.h`](../../external/clap/include/clap/ext/log.h)). That is the only requirement. A host that does not
provide `clap.log` gets no messages and the left channel stays `0`; that is correct too.

Recommended, not required by CLAP:

- Record or show each message with its severity and exact text, including the two non-ASCII characters.
- Keep processing the plug-in after the `FATAL` message. CLAP does not say what a host does with `FATAL`; a host that
  stops or removes the plug-in on it renders no block after step 2 of the protocol.

Messages 6 and 7 are synthetic test messages: they report no real misbehaviour by the host or the plug-in, and exist
only so the host sees each severity once. [`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's
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
| 1 | Load (with `clap.log`), activate and render one block | Message 1 taken; output `1, 0` |
| 2 | Service the main-thread callback | Messages 2 to 7 taken, in order |
| 3 | Render another block (recommended: a host may stop after `FATAL`) | Output `7, 6` |

A host that keeps a log shows the seven messages there with their exact text and severities.

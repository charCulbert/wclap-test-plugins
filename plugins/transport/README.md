# transport

A GUI-free instrument with seven stereo outputs, no inputs, no note ports and no parameters. It writes every field of
the host's transport into its outputs, frame by frame, so a host test can check what the transport said on each frame
from the rendered audio alone. Native CLAP and WCLAP compile the same [`Plugin.cpp`](Plugin.cpp) and
[`Entry.cpp`](Entry.cpp); the sources contain no Wasm-specific branches or APIs.

Plug-in ID: `com.charlieculbert.wclap-test-plugins.transport`

## Proves

The host passes its transport (position, tempo, bar, time signature, play and loop state) in each block's `transport`,
and any change inside a block as a `CLAP_EVENT_TRANSPORT` event on the frame it happens, with the values it means.

## A correct host

**Loads** the plug-in and plays it with a transport. [`events.h`](../../external/clap/include/clap/events.h) (comment
above `clap_event_transport`) says the block's `transport` gives the information "at the start of the block", and a host
that provides sample-accurate tempo or transport changes "can also provide" them as transport events inside the block.
So the block's transport shows on frame 0, and each event shows from its own frame. Sending events inside a block is the
host's choice: a host that sends none shows each block's transport on every frame of the block. A host with no timeline
passes a null `transport` ([`process.h`](../../external/clap/include/clap/process.h): "If null, then this is a free
running host, no transport events will be provided"), and every channel but the event count reads `-1`.
[`tests/smoke.cpp`](tests/smoke.cpp) checks the plug-in's side (ports, every channel from the block's transport and from
events, two events on one frame, withheld fields, a null transport, an event past the block, an event too short to be
a transport) on the native build under `ctest`.

## Ports

| Port | Channels | Left | Right |
| --- | --- | --- | --- |
| Audio output 0, `Transport events / flags` | 2 | Transport events on this frame | `flags` |
| Audio output 1, `Song position beats / seconds` | 2 | `song_pos_beats`, in beats | `song_pos_seconds`, in seconds |
| Audio output 2, `Tempo / tempo increment` | 2 | `tempo`, in BPM | `tempo_inc`, in BPM per sample |
| Audio output 3, `Bar start / bar number` | 2 | `bar_start`, in beats | `bar_number` |
| Audio output 4, `Time signature` | 2 | `tsig_num` | `tsig_denom` |
| Audio output 5, `Loop beats` | 2 | `loop_start_beats`, in beats | `loop_end_beats`, in beats |
| Audio output 6, `Loop seconds` | 2 | `loop_start_seconds`, in seconds | `loop_end_seconds`, in seconds |

Output 0 is the main port. There is no audio input, no note port and there are no parameters. The plug-in returns
`CLAP_PROCESS_CONTINUE` on every valid block (seven stereo outputs with buffers) and `CLAP_PROCESS_ERROR` otherwise. It
never sets `constant_mask`.

## Output

Every channel but the event count holds the latest transport: at frame 0, the block's `transport`; from the frame of
each `CLAP_EVENT_TRANSPORT` event onward, that event's values. A new block starts again from its own `transport`, not
from the last event of the block before. Events apply in list order, so of two transport events on one frame, the later
one holds. An event stamped at or past the end of the block is applied on the block's last frame. Other events are
ignored, and so are transport events whose `size` is smaller than `clap_event_transport`.

**Transport events on this frame** (output 0 left) is the number of `CLAP_EVENT_TRANSPORT` events applied on that
frame: `1` on the frame of one event, `2` if two share it, `0` elsewhere. The block's own `transport` does not count.

**`flags`** (output 0 right) is the flags field as a number, the sum of the
[`events.h`](../../external/clap/include/clap/events.h) `clap_transport_flags` that are set:

| Flag | Value |
| --- | ---: |
| `CLAP_TRANSPORT_HAS_TEMPO` | `1` |
| `CLAP_TRANSPORT_HAS_BEATS_TIMELINE` | `2` |
| `CLAP_TRANSPORT_HAS_SECONDS_TIMELINE` | `4` |
| `CLAP_TRANSPORT_HAS_TIME_SIGNATURE` | `8` |
| `CLAP_TRANSPORT_IS_PLAYING` | `16` |
| `CLAP_TRANSPORT_IS_RECORDING` | `32` |
| `CLAP_TRANSPORT_IS_LOOP_ACTIVE` | `64` |
| `CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL` | `128` |

A transport with all four `HAS_` flags that is playing with its loop on reads `95`.

**Withheld fields.** Each field is read only when the flag that makes it valid is set; otherwise its channel reads `0`.
Read the flags first: a `0` in, say, the tempo channel with flag `1` clear means "no tempo", not 0 BPM.

| Flag | Channels it makes valid |
| --- | --- |
| `CLAP_TRANSPORT_HAS_TEMPO` | `tempo`, `tempo_inc` |
| `CLAP_TRANSPORT_HAS_BEATS_TIMELINE` | `song_pos_beats`, `bar_start`, `bar_number`, `loop_start_beats`, `loop_end_beats` |
| `CLAP_TRANSPORT_HAS_SECONDS_TIMELINE` | `song_pos_seconds`, `loop_start_seconds`, `loop_end_seconds` |
| `CLAP_TRANSPORT_HAS_TIME_SIGNATURE` | `tsig_num`, `tsig_denom` |

[`events.h`](../../external/clap/include/clap/events.h) names a flag for tempo, the beats timeline, the seconds
timeline and the time signature, but does not say which flag covers `bar_start`, `bar_number` or the loop points.
Putting `bar_start`, `bar_number` and the loop points in beats under `CLAP_TRANSPORT_HAS_BEATS_TIMELINE`, and the loop
points in seconds under `CLAP_TRANSPORT_HAS_SECONDS_TIMELINE`, is this plug-in's choice. The loop points are written
whatever `CLAP_TRANSPORT_IS_LOOP_ACTIVE` says, as the host sent them.

**No transport.** When the block's `transport` is null, every channel but the event count reads `-1` from frame 0, until
a transport event in that block (which a host with no transport should not send) replaces it. `flags` never reads `-1`
when there is a transport, so `-1` in output 0 right means "no transport".

**Values are held, not advanced.** Each channel is the value the host sent, on every frame until the next transport
replaces it. The plug-in does not move the position along with the frames, and does not add `tempo_inc` to the tempo
per sample ([`events.h`](../../external/clap/include/clap/events.h): `tempo_inc` is the "tempo increment for each sample
and until the next time info event"); the tempo channel shows `tempo` and the increment channel shows `tempo_inc`.

**Precision.** Positions arrive as fixed point
([`fixedpoint.h`](../../external/clap/include/clap/fixedpoint.h): `CLAP_BEATTIME_FACTOR` and `CLAP_SECTIME_FACTOR` are
`2^31`). The plug-in divides each by `2^31` in double precision and writes it as a 32-bit float, so a value comes out
exact when it is a whole number of `2^-31` steps with at most 24 significant bits, for example `4.5`, `1 + 1/1024` or
`-0.25`. Otherwise it is the nearest float, within `2^-24` of the value relative to its size (about `6e-8 * |value|`).
Tempo, tempo increment, bar number and the time signature are converted to float the same way. Choose test values that
are exact (whole and half beats, tempos such as `120` or `133.25`) and compare exactly, or compare within
`1e-6 * max(1, |value|)`.

## Validation protocol

Render one block of 64 frames with this block `transport` and these events. All `HAS_` flags are set in each (`15`),
and each loop's seconds are its beats at 120 BPM (half the beats).

| Frame | Host sends | `flags` | Beats / seconds | Tempo / inc | Bar start / number | Time signature | Loop beats |
| ---: | --- | ---: | --- | --- | --- | --- | --- |
| 0 | Block `transport`: playing | `31` | `4.5` / `2.25` | `120` / `0` | `4` / `1` | `4` / `4` | `0` / `0` |
| 16 | Transport event: playing, loop on | `95` | `8` / `4` | `90` / `0.125` | `8` / `2` | `3` / `8` | `8` / `16` |
| 32 | Transport event: stopped | `15` | `1.5` / `1` | `60` / `-0.25` | `0` / `0` | `7` / `8` | `0` / `0` |
| 32 | Transport event: playing, after the one above | `31` | `2.5` / `1.25` | `120` / `0` | `0` / `0` | `6` / `8` | `0` / `0` |

Expected output: the event count is `0` on every frame but `1` at frame 16 and `2` at frame 32. Each other channel holds
its row's value on `[0, 16)`, `[16, 32)` and `[32, 64)`, the first frame-32 row being replaced on its own frame by the
second. Then render a block with a null `transport` and no events: every channel but the event count reads `-1`, and
the event count reads `0`.

The smoke test also sends an event a correct host does not send: a transport event stamped at frame 200 of a 64-frame
block, with only `CLAP_TRANSPORT_HAS_TEMPO` (flags `1`, tempo `30`). It applies on the last frame (`[63]`), where every
channel it withholds reads `0`.

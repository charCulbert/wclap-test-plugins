#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 64;
constexpr uint32_t portCount = 7;
constexpr uint32_t channelCount = 2 * portCount;
constexpr char pluginId[] = TEST_PLUGIN_ID;

// Channels in the README's order.
enum Channel : uint32_t
{
    events,
    flags,
    songPosBeats,
    songPosSeconds,
    tempo,
    tempoInc,
    barStart,
    barNumber,
    tsigNum,
    tsigDenom,
    loopStartBeats,
    loopEndBeats,
    loopStartSeconds,
    loopEndSeconds
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

constexpr uint32_t allFields = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
    | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE;

clap_event_transport_t transport(uint32_t time, uint32_t flags, double beats, double seconds,
                                 double bpm, double inc, double barStart, int32_t bar,
                                 uint16_t num, uint16_t denom, double loopStart, double loopEnd)
{
    clap_event_transport_t event {};
    event.header = { sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
    event.flags = flags;
    event.song_pos_beats = std::llround(beats * CLAP_BEATTIME_FACTOR);
    event.song_pos_seconds = std::llround(seconds * CLAP_SECTIME_FACTOR);
    event.tempo = bpm;
    event.tempo_inc = inc;
    event.bar_start = std::llround(barStart * CLAP_BEATTIME_FACTOR);
    event.bar_number = bar;
    event.tsig_num = num;
    event.tsig_denom = denom;
    event.loop_start_beats = std::llround(loopStart * CLAP_BEATTIME_FACTOR);
    event.loop_end_beats = std::llround(loopEnd * CLAP_BEATTIME_FACTOR);
    event.loop_start_seconds = std::llround(loopStart / 2.0 * CLAP_SECTIME_FACTOR);
    event.loop_end_seconds = std::llround(loopEnd / 2.0 * CLAP_SECTIME_FACTOR);
    return event;
}

union AnyEvent
{
    clap_event_header_t header;
    clap_event_transport_t transport;
    clap_event_param_value_t value;
};

struct EventList
{
    std::vector<AnyEvent> list;
    clap_input_events_t events { this, size, get };

    static uint32_t size(const clap_input_events_t* events)
    {
        return static_cast<uint32_t>(static_cast<const EventList*>(events->ctx)->list.size());
    }

    static const clap_event_header_t* get(const clap_input_events_t* events, uint32_t index)
    {
        const auto& list = static_cast<const EventList*>(events->ctx)->list;
        return index < list.size() ? &list[index].header : nullptr;
    }

    void add(const clap_event_transport_t& event)
    {
        AnyEvent any {};
        any.transport = event;
        list.push_back(any);
    }

    void other(uint32_t time)
    {
        AnyEvent any {};
        any.value.header = { sizeof(clap_event_param_value_t), time, CLAP_CORE_EVENT_SPACE_ID,
                             CLAP_EVENT_PARAM_VALUE, 0 };
        any.value.value = 0.5;
        list.push_back(any);
    }
};

struct Block
{
    std::array<std::array<float, blockSize>, channelCount> samples {};
    std::array<float*, channelCount> channels {};
    std::array<clap_audio_buffer_t, portCount> outputs {};

    Block()
    {
        for (uint32_t channel = 0; channel < channelCount; ++channel)
            channels[channel] = samples[channel].data();
        for (uint32_t port = 0; port < portCount; ++port)
            outputs[port] = { &channels[2 * port], nullptr, 2, 0, 0 };
    }

    clap_process_status run(const clap_plugin_t* plugin, const clap_event_transport_t* blockTransport,
                            EventList& events)
    {
        for (auto& channel : samples) channel.fill(-9.0f);
        clap_process_t process {};
        process.frames_count = blockSize;
        process.transport = blockTransport;
        process.audio_outputs = outputs.data();
        process.audio_outputs_count = portCount;
        process.in_events = &events.events;
        return plugin->process(plugin, &process);
    }

    void expect(Channel channel, uint32_t from, uint32_t to, float expected, const char* what) const
    {
        for (auto frame = from; frame < to; ++frame)
            require(samples[channel][frame] == expected,
                    std::string(what) + ": channel " + std::to_string(channel) + " frame "
                        + std::to_string(frame) + " is "
                        + std::to_string(samples[channel][frame]) + ", expected "
                        + std::to_string(expected));
    }
};

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "transport-smoke", "wclap-test-plugins", "", "0.1.0",
        hostGetExtension, hostRequest, hostRequest, hostRequest
    };

    require(clap_entry.init("."), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "factory mismatch");
    const auto* plugin = factory->create_plugin(factory, &host, pluginId);
    require(plugin && plugin->init(plugin), "plugin init failed");

    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    require(ports && ports->count(plugin, true) == 0 && ports->count(plugin, false) == portCount,
            "audio port count mismatch");
    for (uint32_t port = 0; port < portCount; ++port)
    {
        clap_audio_port_info_t info {};
        require(ports->get(plugin, port, false, &info) && info.channel_count == 2
                    && info.id == port,
                "output " + std::to_string(port) + " is not stereo");
    }
    require(!plugin->get_extension(plugin, "clap.note-ports")
                && !plugin->get_extension(plugin, "clap.params"),
            "unexpected note ports or parameters");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");
    Block block;

    // The README's validation protocol.
    const auto playing = allFields | CLAP_TRANSPORT_IS_PLAYING;
    const auto looping = playing | CLAP_TRANSPORT_IS_LOOP_ACTIVE;
    const auto start = transport(0, playing, 4.5, 2.25, 120.0, 0.0, 4.0, 1, 4, 4, 0.0, 0.0);
    EventList list;
    list.other(8);
    list.add(transport(16, looping, 8.0, 4.0, 90.0, 0.125, 8.0, 2, 3, 8, 8.0, 16.0));
    list.other(16);
    list.add(transport(32, allFields, 1.5, 1.0, 60.0, -0.25, 0.0, 0, 7, 8, 0.0, 0.0));
    list.add(transport(32, playing, 2.5, 1.25, 120.0, 0.0, 0.0, 0, 6, 8, 0.0, 0.0));
    // Not sent by a correct host: an event past the end of the block.
    list.add(transport(200, CLAP_TRANSPORT_HAS_TEMPO, 0.0, 0.0, 30.0, 0.0, 0.0, 0, 0, 0, 0.0,
                       0.0));
    require(block.run(plugin, &start, list) == CLAP_PROCESS_CONTINUE, "status mismatch");

    block.expect(events, 0, 16, 0.0f, "no event before frame 16");
    block.expect(events, 16, 17, 1.0f, "event at 16");
    block.expect(events, 17, 32, 0.0f, "no event between");
    block.expect(events, 32, 33, 2.0f, "two events at 32");
    block.expect(events, 33, 63, 0.0f, "no event after");
    block.expect(events, 63, 64, 1.0f, "late event on the last frame");

    block.expect(flags, 0, 16, 31.0f, "block flags");
    block.expect(songPosBeats, 0, 16, 4.5f, "block beats");
    block.expect(songPosSeconds, 0, 16, 2.25f, "block seconds");
    block.expect(tempo, 0, 16, 120.0f, "block tempo");
    block.expect(tempoInc, 0, 16, 0.0f, "block tempo increment");
    block.expect(barStart, 0, 16, 4.0f, "block bar start");
    block.expect(barNumber, 0, 16, 1.0f, "block bar number");
    block.expect(tsigNum, 0, 16, 4.0f, "block numerator");
    block.expect(tsigDenom, 0, 16, 4.0f, "block denominator");
    block.expect(loopStartBeats, 0, 16, 0.0f, "block loop start");
    block.expect(loopEndBeats, 0, 16, 0.0f, "block loop end");

    block.expect(flags, 16, 32, 95.0f, "looping flags");
    block.expect(songPosBeats, 16, 32, 8.0f, "event beats");
    block.expect(songPosSeconds, 16, 32, 4.0f, "event seconds");
    block.expect(tempo, 16, 32, 90.0f, "event tempo");
    block.expect(tempoInc, 16, 32, 0.125f, "event tempo increment");
    block.expect(barStart, 16, 32, 8.0f, "event bar start");
    block.expect(barNumber, 16, 32, 2.0f, "event bar number");
    block.expect(tsigNum, 16, 32, 3.0f, "event numerator");
    block.expect(tsigDenom, 16, 32, 8.0f, "event denominator");
    block.expect(loopStartBeats, 16, 32, 8.0f, "event loop start beats");
    block.expect(loopEndBeats, 16, 32, 16.0f, "event loop end beats");
    block.expect(loopStartSeconds, 16, 32, 4.0f, "event loop start seconds");
    block.expect(loopEndSeconds, 16, 32, 8.0f, "event loop end seconds");

    // Of two events on one frame, the later in the list holds.
    block.expect(flags, 32, 63, 31.0f, "second event's flags");
    block.expect(songPosBeats, 32, 63, 2.5f, "second event's beats");
    block.expect(tempo, 32, 63, 120.0f, "second event's tempo");
    block.expect(tsigNum, 32, 63, 6.0f, "second event's numerator");

    // A field whose flag is clear reads 0.
    block.expect(flags, 63, 64, 1.0f, "tempo-only flags");
    block.expect(tempo, 63, 64, 30.0f, "tempo-only tempo");
    for (const auto channel : { songPosBeats, songPosSeconds, barStart, barNumber, tsigNum,
                                tsigDenom, loopStartBeats, loopEndBeats, loopStartSeconds,
                                loopEndSeconds })
        block.expect(channel, 63, 64, 0.0f, "withheld field");

    // No transport: -1 everywhere but the event channel, until an event.
    EventList late;
    late.add(transport(40, playing, 3.0, 1.5, 120.0, 0.0, 0.0, 0, 4, 4, 0.0, 0.0));
    require(block.run(plugin, nullptr, late) == CLAP_PROCESS_CONTINUE, "status mismatch");
    block.expect(events, 0, 40, 0.0f, "no event with no transport");
    for (uint32_t channel = flags; channel < channelCount; ++channel)
        block.expect(static_cast<Channel>(channel), 0, 40, -1.0f, "no transport");
    block.expect(events, 40, 41, 1.0f, "event with no transport");
    block.expect(songPosBeats, 40, 64, 3.0f, "event after no transport");

    // A new block starts from its own transport, not the last event.
    EventList empty;
    require(block.run(plugin, &start, empty) == CLAP_PROCESS_CONTINUE, "status mismatch");
    block.expect(songPosBeats, 0, blockSize, 4.5f, "next block's beats");
    block.expect(events, 0, blockSize, 0.0f, "next block has no event");

    // Exactness: a beat position with 24 significant bits survives.
    const auto fine = transport(0, allFields, 1.0 + 1.0 / 1024.0, 0.0, 133.25, 0.0, 0.0, 0, 4,
                                4, 0.0, 0.0);
    require(block.run(plugin, &fine, empty) == CLAP_PROCESS_CONTINUE, "status mismatch");
    block.expect(songPosBeats, 0, blockSize, 1.0f + 1.0f / 1024.0f, "fine beats");
    block.expect(tempo, 0, blockSize, 133.25f, "fractional tempo");

    for (uint32_t port = 0; port < portCount; ++port)
        require(block.outputs[port].constant_mask == 0, "constant mask set");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    clap_entry.deinit();
}

} // namespace

int main()
{
    try
    {
        runSmoke();
        std::puts("transport native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "transport native smoke failed: %s\n", error.what());
        return 1;
    }
}

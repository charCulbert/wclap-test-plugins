#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/tail.h>
#include <clap/factory/plugin-factory.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 128;
constexpr char pluginId[] = TEST_PLUGIN_ID;

struct HostState
{
    uint32_t processRequests = 0;
    uint32_t callbackRequests = 0;
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t* host)
{
    ++static_cast<HostState*>(host->host_data)->processRequests;
}
void hostRequestCallback(const clap_host_t* host)
{
    ++static_cast<HostState*>(host->host_data)->callbackRequests;
}

enum class Dialect
{
    midi,
    clap
};

struct EventList
{
    clap_input_events_t events { this, size, get };
    union
    {
        clap_event_header_t header;
        clap_event_midi_t midi;
        clap_event_note_t note;
    } event {};
    bool present = false;
    Dialect dialect = Dialect::midi;

    static uint32_t size(const clap_input_events_t* events)
    {
        return static_cast<const EventList*>(events->ctx)->present ? 1u : 0u;
    }

    static const clap_event_header_t* get(const clap_input_events_t* events,
                                          uint32_t index)
    {
        const auto* self = static_cast<const EventList*>(events->ctx);
        return self->present && index == 0 ? &self->event.header : nullptr;
    }

    // The same note-on in either dialect the note port takes.
    void setKey(int key)
    {
        present = key >= 0;
        if (!present) return;
        event = {};
        if (dialect == Dialect::midi)
        {
            event.midi.header = { sizeof(clap_event_midi_t), 0, CLAP_CORE_EVENT_SPACE_ID,
                                  CLAP_EVENT_MIDI, 0 };
            event.midi.data[0] = 0x90;
            event.midi.data[1] = static_cast<uint8_t>(key);
            event.midi.data[2] = 100;
        }
        else
        {
            event.note = { { sizeof(clap_event_note_t), 0, CLAP_CORE_EVENT_SPACE_ID,
                             CLAP_EVENT_NOTE_ON, 0 },
                           -1, 0, 0, static_cast<int16_t>(key), 0.8 };
        }
    }
};

struct Block
{
    std::array<float, blockSize> signal {};
    std::array<float, blockSize> steady {};
    std::array<float*, 1> signalChannels { signal.data() };
    std::array<float*, 1> steadyChannels { steady.data() };
    std::array<clap_audio_buffer_t, 2> outputs {{
        { signalChannels.data(), nullptr, 1, 0, 0 },
        { steadyChannels.data(), nullptr, 1, 0, 0 },
    }};
    EventList eventList;
    clap_process_t process {};

    clap_process_status run(const clap_plugin_t* plugin, int64_t steadyTime, int key = -1)
    {
        signal.fill(-1.0f);
        steady.fill(-1.0f);
        eventList.setKey(key);
        process = {};
        process.steady_time = steadyTime;
        process.frames_count = blockSize;
        process.audio_outputs = outputs.data();
        process.audio_outputs_count = outputs.size();
        process.in_events = &eventList.events;
        return plugin->process(plugin, &process);
    }
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void requireConstant(const std::array<float, blockSize>& samples, float expected,
                     const char* message)
{
    require(std::all_of(samples.begin(), samples.end(), [expected](float value) {
        return std::abs(value - expected) < 1.0e-6f;
    }), message);
}

float advance(int64_t frames) { return static_cast<float>(frames / 65'536.0); }

// One activation of the whole protocol, with note-ons in one dialect.
void runProtocol(const clap_plugin_t* plugin, const clap_plugin_tail_t* tail, HostState& state,
                 Dialect dialect)
{
    state = {};
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");

    Block block;
    block.eventList.dialect = dialect;
    // Steady time does not start at 0: only its growth is specified.
    const int64_t t0 = 1'000'000;

    require(block.run(plugin, t0) == CLAP_PROCESS_SLEEP, "initial status mismatch");
    requireConstant(block.signal, 0.1f, "initial marker mismatch");
    requireConstant(block.steady, 0.0f, "first call has no steady time advance");
    require(state.callbackRequests == 1, "request_callback was not issued");

    plugin->on_main_thread(plugin);
    require(state.processRequests == 1, "request_process was not issued on main thread");
    require(block.run(plugin, t0 + blockSize) == CLAP_PROCESS_SLEEP,
            "request_process wake status mismatch");
    requireConstant(block.signal, 0.2f, "request_process wake marker mismatch");
    requireConstant(block.steady, advance(blockSize), "contiguous steady time advance");

    // Three blocks skipped: the advance includes them.
    require(block.run(plugin, t0 + 5 * blockSize, 63) == CLAP_PROCESS_CONTINUE,
            "event wake status mismatch");
    requireConstant(block.signal, 0.5f, "event wake marker mismatch");
    requireConstant(block.steady, advance(4 * blockSize),
                    "steady time advance did not include skipped blocks");

    require(block.run(plugin, t0 + 6 * blockSize, 62) == CLAP_PROCESS_SLEEP,
            "sleep status mismatch");
    requireConstant(block.signal, 0.0f, "sleep marker mismatch");
    requireConstant(block.steady, advance(blockSize), "sleep steady time advance");
    // A host may keep processing a sleeping plug-in.
    require(block.run(plugin, t0 + 7 * blockSize) == CLAP_PROCESS_SLEEP,
            "sleep status mismatch on a further call");
    requireConstant(block.signal, 0.0f, "sleep marker mismatch on a further call");

    require(block.run(plugin, t0 + 8 * blockSize, 61) == CLAP_PROCESS_CONTINUE_IF_NOT_QUIET,
            "audible quiet-aware status mismatch");
    requireConstant(block.signal, 0.4f, "audible quiet-aware marker mismatch");
    require(block.run(plugin, t0 + 9 * blockSize) == CLAP_PROCESS_CONTINUE_IF_NOT_QUIET,
            "quiet quiet-aware status mismatch");
    requireConstant(block.signal, 0.0f, "quiet-aware signal was not quiet");
    requireConstant(block.steady, 0.0f, "quiet-aware diagnostic output was not quiet");

    require(block.run(plugin, t0 + 10 * blockSize, 60) == CLAP_PROCESS_TAIL,
            "finite tail status mismatch");
    require(tail->get(plugin) == 256, "finite tail length mismatch");
    requireConstant(block.signal, 0.3f, "finite tail marker mismatch");

    require(block.run(plugin, t0 + 11 * blockSize, 64) == CLAP_PROCESS_TAIL,
            "infinite tail status mismatch");
    require(tail->get(plugin) >= 0x7fffffffU, "infinite tail length mismatch");
    requireConstant(block.signal, 0.6f, "infinite tail marker mismatch");

    // Broken steady time: no growth is marked -1; not available (-1) is 0.
    require(block.run(plugin, t0 + 11 * blockSize) == CLAP_PROCESS_TAIL, "tail status mismatch");
    requireConstant(block.steady, -1.0f, "steady time that did not grow was not marked");
    require(block.run(plugin, -1) == CLAP_PROCESS_TAIL, "tail status mismatch");
    requireConstant(block.steady, 0.0f, "unavailable steady time was not 0");

    require(block.run(plugin, -1, 65) == CLAP_PROCESS_ERROR, "error status mismatch");
    requireConstant(block.signal, 9.0f, "error poison marker mismatch");
    requireConstant(block.steady, 9.0f, "error poison diagnostic mismatch");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
}

void runSmoke()
{
    HostState state;
    const clap_host_t host {
        CLAP_VERSION, &state, "process-lifecycle-smoke", "wclap-test-plugins",
        "", "0.1.0", hostGetExtension, hostRequestRestart, hostRequestProcess,
        hostRequestCallback
    };

    require(clap_entry.init("."), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "factory mismatch");
    require(factory->get_plugin_descriptor(factory, 0)
                && std::strcmp(factory->get_plugin_descriptor(factory, 0)->id, pluginId) == 0,
            "descriptor mismatch");

    const auto* plugin = factory->create_plugin(factory, &host, pluginId);
    require(plugin && plugin->init(plugin), "plugin init failed");

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    require(audioPorts && audioPorts->count(plugin, false) == 2
                && audioPorts->count(plugin, true) == 0,
            "audio port mismatch");
    clap_note_port_info_t noteInfo {};
    require(notePorts && notePorts->count(plugin, true) == 1
                && notePorts->get(plugin, 0, true, &noteInfo)
                && noteInfo.supported_dialects == (CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI)
                && noteInfo.preferred_dialect == CLAP_NOTE_DIALECT_MIDI,
            "note port mismatch");
    require(tail, "tail extension missing");

    // The port takes both dialects, so the host may send either; the second
    // activation also checks that the protocol starts over.
    runProtocol(plugin, tail, state, Dialect::midi);
    runProtocol(plugin, tail, state, Dialect::clap);

    plugin->destroy(plugin);
    clap_entry.deinit();
}

} // namespace

int main()
{
    try
    {
        runSmoke();
        std::puts("process-lifecycle native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "process-lifecycle native smoke failed: %s\n", error.what());
        return 1;
    }
}

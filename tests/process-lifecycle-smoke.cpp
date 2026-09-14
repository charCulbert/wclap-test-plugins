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
constexpr char pluginId[] =
    "com.charlieculbert.wclap-test-plugins.process-lifecycle";

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

struct EventList
{
    clap_input_events_t events { this, size, get };
    clap_event_midi_t midi {};
    bool present = false;

    static uint32_t size(const clap_input_events_t* events)
    {
        return static_cast<const EventList*>(events->ctx)->present ? 1u : 0u;
    }

    static const clap_event_header_t* get(const clap_input_events_t* events,
                                          uint32_t index)
    {
        const auto* self = static_cast<const EventList*>(events->ctx);
        return self->present && index == 0 ? &self->midi.header : nullptr;
    }

    void setKey(int key)
    {
        present = key >= 0;
        if (!present) return;
        midi = {};
        midi.header.size = sizeof(midi);
        midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        midi.header.type = CLAP_EVENT_MIDI;
        midi.port_index = 0;
        midi.data[0] = 0x90;
        midi.data[1] = static_cast<uint8_t>(key);
        midi.data[2] = 100;
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
    require(notePorts && notePorts->count(plugin, true) == 1, "note port mismatch");
    require(tail, "tail extension missing");
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");

    Block block;
    require(block.run(plugin, 0) == CLAP_PROCESS_SLEEP, "initial status mismatch");
    requireConstant(block.signal, 0.1f, "initial marker mismatch");
    require(state.callbackRequests == 1, "request_callback was not issued");

    plugin->on_main_thread(plugin);
    require(state.processRequests == 1, "request_process was not issued on main thread");
    require(block.run(plugin, blockSize) == CLAP_PROCESS_SLEEP,
            "request_process wake status mismatch");
    requireConstant(block.signal, 0.2f, "request_process wake marker mismatch");

    require(block.run(plugin, 5 * blockSize, 63) == CLAP_PROCESS_CONTINUE,
            "event wake status mismatch");
    requireConstant(block.signal, 0.5f, "event wake marker mismatch");
    requireConstant(block.steady, static_cast<float>(5 * blockSize / 65'536.0),
                    "steady time did not include skipped blocks");

    require(block.run(plugin, 6 * blockSize, 61)
                == CLAP_PROCESS_CONTINUE_IF_NOT_QUIET,
            "audible quiet-aware status mismatch");
    requireConstant(block.signal, 0.4f, "audible quiet-aware marker mismatch");
    require(block.run(plugin, 7 * blockSize) == CLAP_PROCESS_CONTINUE_IF_NOT_QUIET,
            "quiet quiet-aware status mismatch");
    requireConstant(block.signal, 0.0f, "quiet-aware signal was not quiet");
    requireConstant(block.steady, 0.0f, "quiet-aware diagnostic output was not quiet");

    require(block.run(plugin, 8 * blockSize, 60) == CLAP_PROCESS_TAIL,
            "finite tail status mismatch");
    require(tail->get(plugin) == 256, "finite tail length mismatch");
    requireConstant(block.signal, 0.3f, "finite tail marker mismatch");

    require(block.run(plugin, 9 * blockSize, 64) == CLAP_PROCESS_TAIL,
            "infinite tail status mismatch");
    require(tail->get(plugin) >= 0x7fffffffU, "infinite tail length mismatch");
    requireConstant(block.signal, 0.6f, "infinite tail marker mismatch");

    require(block.run(plugin, 10 * blockSize, 65) == CLAP_PROCESS_ERROR,
            "error status mismatch");
    requireConstant(block.signal, 9.0f, "error poison marker mismatch");
    requireConstant(block.steady, 9.0f, "error poison diagnostic mismatch");

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
        std::puts("process-lifecycle native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "process-lifecycle native smoke failed: %s\n", error.what());
        return 1;
    }
}

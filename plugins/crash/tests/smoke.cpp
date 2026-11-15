// Never sets Crash to 1 in process, or through flush followed by process: a
// native build would stop this test there. The trap is checked on the WCLAP.

#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 64;
constexpr char effectId[] = TEST_PLUGIN_ID;
constexpr char instrumentId[] = TEST_PLUGIN_ID ".instrument";
constexpr clap_id crashParamId = 0;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

struct EventList
{
    std::vector<clap_event_param_value_t> list;
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

    void value(uint32_t time, double value, clap_id id = crashParamId)
    {
        clap_event_param_value_t event {};
        event.header = { sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE,
                         0 };
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        list.push_back(event);
    }
};

// Output events from flush; the plug-in sends none.
struct OutputList
{
    uint32_t pushed = 0;
    clap_output_events_t events { this, push };

    static bool push(const clap_output_events_t* events, const clap_event_header_t*)
    {
        ++static_cast<OutputList*>(events->ctx)->pushed;
        return true;
    }
};

struct Block
{
    std::array<float, blockSize> inLeft {};
    std::array<float, blockSize> inRight {};
    std::array<float, blockSize> left {};
    std::array<float, blockSize> right {};
    std::array<float*, 2> inputChannels { inLeft.data(), inRight.data() };
    std::array<float*, 2> outputChannels { left.data(), right.data() };
    clap_audio_buffer_t input { inputChannels.data(), nullptr, 2, 0, 0 };
    clap_audio_buffer_t output { outputChannels.data(), nullptr, 2, 0, 0 };

    clap_process_status run(const clap_plugin_t* plugin, bool withInput, EventList& events)
    {
        for (uint32_t frame = 0; frame < blockSize; ++frame)
        {
            inLeft[frame] = 0.25f + frame / 128.0f;
            inRight[frame] = -0.5f + frame / 256.0f;
        }
        left.fill(-9.0f);
        right.fill(-9.0f);
        clap_process_t process {};
        process.frames_count = blockSize;
        process.audio_inputs = withInput ? &input : nullptr;
        process.audio_inputs_count = withInput ? 1 : 0;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1;
        process.in_events = &events.events;
        return plugin->process(plugin, &process);
    }
};

void checkPlugin(const clap_plugin_factory_t* factory, const clap_host_t& host, const char* id,
                 bool instrument)
{
    const std::string name = id;
    const auto* plugin = factory->create_plugin(factory, &host, id);
    require(plugin && plugin->init(plugin), name + ": init failed");

    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    require(ports && ports->count(plugin, true) == (instrument ? 0u : 1u)
                && ports->count(plugin, false) == 1,
            name + ": audio port count mismatch");
    clap_audio_port_info_t info {};
    require(ports->get(plugin, 0, false, &info) && info.channel_count == 2
                && info.port_type && std::strcmp(info.port_type, CLAP_PORT_STEREO) == 0,
            name + ": output is not stereo");
    if (!instrument)
        require(ports->get(plugin, 0, true, &info) && info.channel_count == 2,
                name + ": input is not stereo");

    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    clap_param_info_t param {};
    require(params && params->count(plugin) == 1 && params->get_info(plugin, 0, &param)
                && param.id == crashParamId && std::strcmp(param.name, "Crash") == 0
                && param.min_value == 0.0 && param.max_value == 1.0
                && param.default_value == 0.0
                && param.flags == (CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE),
            name + ": parameter info mismatch");
    double value = -1.0;
    require(params->get_value(plugin, crashParamId, &value) && value == 0.0,
            name + ": Crash does not start at 0");

    char text[32] {};
    require(params->value_to_text(plugin, crashParamId, 1.0, text, sizeof(text))
                && std::strcmp(text, "Crash") == 0
                && params->text_to_value(plugin, crashParamId, text, &value) && value == 1.0,
            name + ": text round trip for 1 failed");
    require(params->value_to_text(plugin, crashParamId, 0.0, text, sizeof(text))
                && std::strcmp(text, "Off") == 0
                && params->text_to_value(plugin, crashParamId, text, &value) && value == 0.0,
            name + ": text round trip for 0 failed");
    require(!params->text_to_value(plugin, crashParamId, "junk", &value),
            name + ": junk text accepted");

    // A stepped value converts by truncation (ext/params.h): 1.7 is 1, 0.5 is 0, NaN is 0.
    const auto textFor = [&](double v) {
        require(params->value_to_text(plugin, crashParamId, v, text, sizeof(text)),
                name + ": value_to_text failed");
        return std::string(text);
    };
    require(textFor(1.7) == "Crash" && textFor(0.5) == "Off" && textFor(0.99) == "Off"
                && textFor(std::nan("")) == "Off" && textFor(-0.5) == "Off",
            name + ": value_to_text does not truncate");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), name + ": activate failed");
    require(plugin->start_processing(plugin), name + ": start processing failed");

    // Crash at 0, values that truncate to 0, NaN, and another parameter's ID: no trap.
    EventList events;
    events.value(4, 0.0);
    events.value(8, 0.5);
    events.value(10, 0.99);
    events.value(12, -3.0);
    events.value(14, std::nan(""));
    events.value(16, 1.0, 7);
    Block block;
    require(block.run(plugin, !instrument, events) == CLAP_PROCESS_CONTINUE,
            name + ": status mismatch");
    for (uint32_t frame = 0; frame < blockSize; ++frame)
    {
        const float wantLeft = instrument ? 0.5f : block.inLeft[frame];
        const float wantRight = instrument ? 0.5f : block.inRight[frame];
        require(block.left[frame] == wantLeft && block.right[frame] == wantRight,
                name + ": output mismatch at frame " + std::to_string(frame));
    }
    require(params->get_value(plugin, crashParamId, &value) && value == 0.0,
            name + ": Crash is not 0 after truncation");

    // In place: the effect leaves a shared buffer as it is.
    if (!instrument)
    {
        block.outputChannels = block.inputChannels;
        EventList none;
        require(block.run(plugin, true, none) == CLAP_PROCESS_CONTINUE,
                name + ": in-place status mismatch");
        for (uint32_t frame = 0; frame < blockSize; ++frame)
            require(block.inLeft[frame] == 0.25f + frame / 128.0f
                        && block.inRight[frame] == -0.5f + frame / 256.0f,
                    name + ": in-place output changed at frame " + std::to_string(frame));
        block.outputChannels = { block.left.data(), block.right.data() };
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);

    // flush stores Crash without trapping; 1.7 would trap on the next process
    // call, so it is checked through get_value and set back before any.
    OutputList out;
    const auto flushValue = [&](double v) {
        EventList list;
        list.value(0, v);
        params->flush(plugin, &list.events, &out.events);
        double stored = -1.0;
        require(params->get_value(plugin, crashParamId, &stored), name + ": get_value failed");
        return stored;
    };
    require(flushValue(1.7) == 1.0, name + ": flush did not store 1.7 as 1");
    require(flushValue(0.5) == 0.0, name + ": flush did not store 0.5 as 0");
    require(flushValue(1.0) == 1.0, name + ": flush did not store Crash");
    require(flushValue(std::nan("")) == 0.0, name + ": flush did not store NaN as 0");
    require(out.pushed == 0, name + ": flush sent output events");

    plugin->destroy(plugin);
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "crash-smoke", "wclap-test-plugins", "", "0.1.0",
        hostGetExtension, hostRequest, hostRequest, hostRequest
    };

    require(clap_entry.init("."), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 2, "factory mismatch");
    const auto* first = factory->get_plugin_descriptor(factory, 0);
    const auto* second = factory->get_plugin_descriptor(factory, 1);
    require(first && std::strcmp(first->id, effectId) == 0 && second
                && std::strcmp(second->id, instrumentId) == 0,
            "descriptor IDs mismatch");
    require(!factory->create_plugin(factory, &host, TEST_PLUGIN_ID ".other"),
            "created an unknown ID");

    checkPlugin(factory, host, effectId, false);
    checkPlugin(factory, host, instrumentId, true);
    clap_entry.deinit();
}

} // namespace

int main()
{
    try
    {
        runSmoke();
        std::puts("crash native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "crash native smoke failed: %s\n", error.what());
        return 1;
    }
}

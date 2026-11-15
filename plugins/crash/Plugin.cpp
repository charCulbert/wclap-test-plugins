#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wclap_test_plugins::crash
{
namespace
{

// One bundle, two plug-ins with the same parameter: an effect whose input and
// output ports match, and an instrument with only an output.
constexpr char effectId[] = TEST_PLUGIN_ID;
constexpr char instrumentId[] = TEST_PLUGIN_ID ".instrument";
constexpr clap_id crashParamId = 0;
constexpr float instrumentLevel = 0.5f;

const char* effectFeatures[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                               nullptr };
const char* instrumentFeatures[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                   nullptr };

const clap_plugin_descriptor_t descriptors[] {
    { CLAP_VERSION, effectId, "WCLAP Crash Test", "Charlie Culbert", TEST_PLUGIN_URL,
      TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
      "Passes stereo audio through, and traps in process when its Crash parameter is set to 1",
      effectFeatures },
    { CLAP_VERSION, instrumentId, "WCLAP Crash Instrument Test", "Charlie Culbert",
      TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
      "Writes a constant 0.5 to its stereo output, and traps in process when its Crash "
      "parameter is set to 1",
      instrumentFeatures },
};

constexpr uint32_t descriptorCount = sizeof(descriptors) / sizeof(descriptors[0]);

// The parameter is stepped, so a value converts to a whole number by
// truncation (ext/params.h), then clamps to 0 or 1. NaN reads 0.
double step(double value) noexcept
{
    return std::isnan(value) ? 0.0 : std::clamp(std::trunc(value), 0.0, 1.0) + 0.0;
}

[[noreturn]] void trap() noexcept
{
    // A WebAssembly `unreachable` in a WCLAP; a native build stops the process.
#ifdef _MSC_VER
    std::abort();
#else
    __builtin_trap();
#endif
}

class CrashPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    CrashPlugin(const clap_plugin_descriptor_t* descriptor, const clap_host_t* host,
                bool instrument)
        : Base(descriptor, host), instrument(instrument)
    {
    }

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override { return true; }
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        // Crash set to 1 through params.flush traps on the next block.
        if (crash.load(std::memory_order_relaxed) == 1.0) trap();

        if (!process || process->audio_outputs_count != 1
            || process->audio_inputs_count != (instrument ? 0u : 1u))
            return CLAP_PROCESS_ERROR;
        const auto& output = process->audio_outputs[0];
        if (output.channel_count != 2 || !output.data32) return CLAP_PROCESS_ERROR;
        const auto frames = process->frames_count;
        for (uint32_t channel = 0; channel < 2; ++channel)
        {
            auto* out = output.data32[channel];
            if (!out) return CLAP_PROCESS_ERROR;
            if (instrument)
            {
                std::fill_n(out, frames, instrumentLevel);
                continue;
            }
            const auto& input = process->audio_inputs[0];
            if (input.channel_count != 2 || !input.data32 || !input.data32[channel])
                return CLAP_PROCESS_ERROR;
            std::memmove(out, input.data32[channel], frames * sizeof(float));
        }

        // The trap ends the call, so the block's audio does not matter after it.
        const auto* events = process->in_events;
        const auto count = events ? events->size(events) : 0;
        for (uint32_t index = 0; index < count; ++index)
            if (const auto* header = events->get(events, index); header && apply(header))
                trap();
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        return isInput && instrument ? 0 : 1;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (index != 0 || !info || (isInput && instrument)) return false;
        *info = {};
        info->id = 0;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = instrument ? CLAP_INVALID_ID : 0;
        std::snprintf(info->name, sizeof(info->name), "%s",
                      isInput ? "Input" : instrument ? "Constant 0.5" : "Input, unchanged");
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return 1; }
    bool paramsInfo(uint32_t index, clap_param_info_t* info) const noexcept override
    {
        if (index != 0 || !info) return false;
        *info = {};
        info->id = crashParamId;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        std::snprintf(info->name, sizeof(info->name), "Crash");
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    }

    bool paramsValue(clap_id paramId, double* value) noexcept override
    {
        if (paramId != crashParamId || !value) return false;
        *value = crash.load(std::memory_order_relaxed);
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char* display,
                           uint32_t size) noexcept override
    {
        if (paramId != crashParamId || !display || size == 0) return false;
        std::snprintf(display, size, "%s", step(value) == 1.0 ? "Crash" : "Off");
        return true;
    }

    bool paramsTextToValue(clap_id paramId, const char* display,
                           double* value) noexcept override
    {
        if (paramId != crashParamId || !display || !value) return false;
        if (std::strcmp(display, "Off") == 0) *value = 0.0;
        else if (std::strcmp(display, "Crash") == 0) *value = 1.0;
        else return false;
        return true;
    }

    // Stores the value. The trap comes on the next process call.
    void paramsFlush(const clap_input_events_t* in, const clap_output_events_t*) noexcept override
    {
        const auto count = in ? in->size(in) : 0;
        for (uint32_t index = 0; index < count; ++index)
            if (const auto* header = in->get(in, index)) apply(header);
    }

private:
    // Applies a Crash value and returns true if it set Crash to 1.
    bool apply(const clap_event_header_t* header) noexcept
    {
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t))
            return false;
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        if (event->param_id != crashParamId) return false;
        const auto value = step(event->value);
        crash.store(value, std::memory_order_relaxed);
        return value == 1.0;
    }

    const bool instrument;
    std::atomic<double> crash { 0.0 };
};

uint32_t pluginCount(const clap_plugin_factory_t*) { return descriptorCount; }

const clap_plugin_descriptor_t* pluginDescriptor(const clap_plugin_factory_t*,
                                                 uint32_t index)
{
    return index < descriptorCount ? &descriptors[index] : nullptr;
}

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
                                  const clap_host_t* host, const char* id)
{
    if (!host || !id) return nullptr;
    if (std::strcmp(id, effectId) == 0)
        return (new CrashPlugin(&descriptors[0], host, false))->clapPlugin();
    if (std::strcmp(id, instrumentId) == 0)
        return (new CrashPlugin(&descriptors[1], host, true))->clapPlugin();
    return nullptr;
}

} // namespace

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    if (!factoryId || std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) != 0)
        return nullptr;
    static const clap_plugin_factory_t factory {
        pluginCount, pluginDescriptor, createPlugin
    };
    return &factory;
}

} // namespace wclap_test_plugins::crash

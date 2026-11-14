#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wclap_test_plugins::param_indication
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr clap_id targetParamId = 1;

// 0xRRGGBB as an integer (exact in a float), or -1 for no colour. A fully
// transparent colour (alpha 0, as CLAP_COLOR_TRANSPARENT) shows nothing, so it
// reads -1 too. Other alpha values are not shown.
int32_t encodeColor(const clap_color_t* color) noexcept
{
    if (!color || color->alpha == 0) return -1;
    return (color->red << 16) | (color->green << 8) | color->blue;
}

class ParamIndicationPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit ParamIndicationPlugin(const clap_host_t* host) : Base(&descriptor(), host) {}

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override { return true; }
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != 2) return CLAP_PROCESS_ERROR;
        const int32_t values[2][2] {
            { mapping.load(std::memory_order_relaxed),
              mappingColor.load(std::memory_order_relaxed) },
            { automation.load(std::memory_order_relaxed),
              automationColor.load(std::memory_order_relaxed) },
        };
        for (uint32_t port = 0; port < 2; ++port)
        {
            auto& output = process->audio_outputs[port];
            if (output.channel_count != 2 || !output.data32) return CLAP_PROCESS_ERROR;
            for (uint32_t channel = 0; channel < 2; ++channel)
            {
                if (!output.data32[channel]) return CLAP_PROCESS_ERROR;
                std::fill_n(output.data32[channel], process->frames_count,
                            static_cast<float>(values[port][channel]));
            }
            output.constant_mask = 0b11;
        }

        // Parameter values are applied but not shown.
        const auto* events = process->in_events;
        const auto count = events ? events->size(events) : 0;
        for (uint32_t index = 0; index < count; ++index)
            if (const auto* header = events->get(events, index)) apply(header);
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return isInput ? 0 : 2; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (isInput || index >= 2 || !info) return false;
        *info = {};
        info->id = index;
        info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "%s",
                      index == 0 ? "Mapping / colour" : "Automation state / colour");
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return 1; }
    bool paramsInfo(uint32_t index, clap_param_info_t* info) const noexcept override
    {
        if (index != 0 || !info) return false;
        *info = {};
        info->id = targetParamId;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::snprintf(info->name, sizeof(info->name), "Target");
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    }
    bool paramsValue(clap_id paramId, double* value) noexcept override
    {
        if (paramId != targetParamId || !value) return false;
        *value = target.load(std::memory_order_relaxed);
        return true;
    }
    bool paramsValueToText(clap_id paramId, double value, char* display,
                           uint32_t size) noexcept override
    {
        if (paramId != targetParamId || !display || size == 0) return false;
        std::snprintf(display, size, "%.6f", value);
        return true;
    }
    bool paramsTextToValue(clap_id paramId, const char* display,
                           double* value) noexcept override
    {
        if (paramId != targetParamId || !display || !value) return false;
        char* end = nullptr;
        *value = std::strtod(display, &end);
        return end != display;
    }
    void paramsFlush(const clap_input_events_t* in, const clap_output_events_t*) noexcept override
    {
        const auto count = in ? in->size(in) : 0;
        for (uint32_t index = 0; index < count; ++index)
            if (const auto* header = in->get(in, index)) apply(header);
    }

    bool implementsParamIndication() const noexcept override { return true; }
    void paramIndicationSetMapping(clap_id, bool hasMapping, const clap_color_t* color,
                                   const char*, const char*) noexcept override
    {
        mapping.store(hasMapping ? 1 : 0, std::memory_order_relaxed);
        mappingColor.store(encodeColor(color), std::memory_order_relaxed);
    }
    void paramIndicationSetAutomation(clap_id, uint32_t state,
                                      const clap_color_t* color) noexcept override
    {
        automation.store(static_cast<int32_t>(state), std::memory_order_relaxed);
        automationColor.store(encodeColor(color), std::memory_order_relaxed);
    }

private:
    void apply(const clap_event_header_t* header) noexcept
    {
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t))
            return;
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        if (event->param_id == targetParamId)
            target.store(event->value, std::memory_order_relaxed);
    }

    std::atomic<double> target { 0.0 };
    std::atomic<int32_t> mapping { -1 };
    std::atomic<int32_t> mappingColor { -1 };
    std::atomic<int32_t> automation { -1 };
    std::atomic<int32_t> automationColor { -1 };
};

uint32_t pluginCount(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* pluginDescriptor(const clap_plugin_factory_t*,
                                                 uint32_t index)
{
    return index == 0 ? &descriptor() : nullptr;
}

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
                                  const clap_host_t* host, const char* id)
{
    if (!host || !id || std::strcmp(id, pluginId) != 0) return nullptr;
    return (new ParamIndicationPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                    nullptr };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Parameter Indication Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Writes the mapping and automation indication the host last set, with their colours, to "
        "its outputs",
        features
    };
    return value;
}

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

} // namespace wclap_test_plugins::param_indication

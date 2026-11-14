#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::render
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;

class RenderPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit RenderPlugin(const clap_host_t* host) : Base(&descriptor(), host) {}

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override { return true; }
    bool startProcessing() noexcept override { return true; }

    // Writes the current render mode plus one: 1 real time, 2 offline.
    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != 1) return CLAP_PROCESS_ERROR;
        auto& output = process->audio_outputs[0];
        if (output.channel_count != 1 || !output.data32 || !output.data32[0])
            return CLAP_PROCESS_ERROR;
        const auto value = static_cast<float>(mode.load(std::memory_order_relaxed) + 1);
        std::fill_n(output.data32[0], process->frames_count, value);
        output.constant_mask = 1;
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return isInput ? 0 : 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (isInput || index != 0 || !info) return false;
        *info = {};
        info->id = 0;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 1;
        info->port_type = CLAP_PORT_MONO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "Render mode");
        return true;
    }

    // clap.render is served here rather than through clap-helpers, which ends
    // the process on a mode it does not know. render.h: set returns whether
    // the mode could be applied, so an unknown mode returns false.
    const void* extension(const char* id) noexcept override
    {
        static const clap_plugin_render_t render { hasHardRealtimeRequirement, setMode };
        return id && std::strcmp(id, CLAP_EXT_RENDER) == 0 ? &render : nullptr;
    }

private:
    static bool hasHardRealtimeRequirement(const clap_plugin_t*) noexcept { return false; }

    static bool setMode(const clap_plugin_t* plugin, clap_plugin_render_mode newMode) noexcept
    {
        if (!plugin || !plugin->plugin_data) return false;
        if (newMode != CLAP_RENDER_REALTIME && newMode != CLAP_RENDER_OFFLINE) return false;
        auto& self = *static_cast<RenderPlugin*>(static_cast<Base*>(plugin->plugin_data));
        self.mode.store(newMode, std::memory_order_relaxed);
        return true;
    }

    std::atomic<int32_t> mode { CLAP_RENDER_REALTIME };
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
    return (new RenderPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                    nullptr };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Render Mode Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Writes the render mode the host last set (1 real time, 2 offline) to its output",
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

} // namespace wclap_test_plugins::render

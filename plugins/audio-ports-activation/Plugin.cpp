#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::audio_ports_activation
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr uint32_t outputCount = 2;

// Ignore, not Terminate: a set_active call while active is reported through
// the host's log and refused, so a host test sees the refusal.
using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>;

class AudioPortsActivationPlugin final : public Base
{
public:
    explicit AudioPortsActivationPlugin(const clap_host_t* host) : Base(&descriptor(), host) {}

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override { return true; }
    bool startProcessing() noexcept override { return true; }

    // An active output k writes k + 1; an inactive one writes 0 and sets its
    // constant_mask.
    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != outputCount) return CLAP_PROCESS_ERROR;
        for (uint32_t port = 0; port < outputCount; ++port)
        {
            auto& output = process->audio_outputs[port];
            if (output.channel_count != 1 || !output.data32 || !output.data32[0])
                return CLAP_PROCESS_ERROR;
            const auto value = active[port] ? static_cast<float>(port + 1) : 0.0f;
            std::fill_n(output.data32[0], process->frames_count, value);
            output.constant_mask = 1;
        }
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        return isInput ? 0 : outputCount;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (isInput || index >= outputCount || !info) return false;
        *info = {};
        info->id = index;
        info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
        info->channel_count = 1;
        info->port_type = CLAP_PORT_MONO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "Output %u", index + 1);
        return true;
    }

    bool implementsAudioPortsActivation() const noexcept override { return true; }
    bool audioPortsActivationCanActivateWhileProcessing() const noexcept override
    {
        return false;
    }
    bool audioPortsActivationSetActive(bool isInput, uint32_t portIndex, bool isActive,
                                       uint32_t sampleSize) noexcept override
    {
        // Only 32-bit buffers are supported.
        if (isInput || portIndex >= outputCount || (sampleSize != 0 && sampleSize != 32))
            return false;
        active[portIndex] = isActive;
        return true;
    }

private:
    std::array<bool, outputCount> active { true, true };
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
    return (new AudioPortsActivationPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                    nullptr };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Audio Ports Activation Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Writes a marker to each output the host keeps active and silence to each it deactivates",
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

} // namespace wclap_test_plugins::audio_ports_activation

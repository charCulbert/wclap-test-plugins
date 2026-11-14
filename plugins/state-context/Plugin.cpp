#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::state_context
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
// Stored little-endian, so the first four bytes read "CSTW".
constexpr uint32_t stateMagic = 0x57545343;
constexpr uint32_t stateVersion = 1;
// state.save and state.load have no context; the plug-in records them as 0,
// and treats a context it does not know the same way.
constexpr uint32_t plainState = 0;

// The whole saved state: which call saved it.
struct State
{
    uint32_t magic;
    uint32_t version;
    uint32_t savedFor;
};

bool knownContext(uint32_t context) noexcept
{
    return context == CLAP_STATE_CONTEXT_FOR_PRESET || context == CLAP_STATE_CONTEXT_FOR_DUPLICATE
        || context == CLAP_STATE_CONTEXT_FOR_PROJECT;
}

// A context added after this plug-in was built is handled as plain state, the
// fallback state-context.h recommends.
uint32_t recorded(uint32_t context) noexcept
{
    return knownContext(context) ? context : plainState;
}

class StateContextPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit StateContextPlugin(const clap_host_t* host) : Base(&descriptor(), host) {}

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override { return true; }
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != 1) return CLAP_PROCESS_ERROR;
        auto& output = process->audio_outputs[0];
        if (output.channel_count != 2 || !output.data32) return CLAP_PROCESS_ERROR;
        auto* left = output.data32[0];
        auto* right = output.data32[1];
        if (!left || !right) return CLAP_PROCESS_ERROR;
        std::fill_n(left, process->frames_count,
                    static_cast<float>(loadedSavedFor.load(std::memory_order_relaxed)));
        std::fill_n(right, process->frames_count,
                    static_cast<float>(loadedWith.load(std::memory_order_relaxed)));
        output.constant_mask = 0b11;
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
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "Saved for / loaded with");
        return true;
    }

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream_t* stream) noexcept override
    {
        return save(stream, plainState);
    }
    bool stateLoad(const clap_istream_t* stream) noexcept override
    {
        return load(stream, plainState);
    }

    bool implementsStateContext() const noexcept override { return true; }
    bool stateContextSave(const clap_ostream_t* stream, uint32_t context) noexcept override
    {
        return save(stream, recorded(context));
    }
    bool stateContextLoad(const clap_istream_t* stream, uint32_t context) noexcept override
    {
        return load(stream, recorded(context));
    }

private:
    static bool save(const clap_ostream_t* stream, uint32_t context) noexcept
    {
        if (!stream) return false;
        const State state { stateMagic, stateVersion, context };
        const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
        for (size_t done = 0; done < sizeof(state);)
        {
            const auto written = stream->write(stream, bytes + done, sizeof(state) - done);
            if (written <= 0) return false;
            done += static_cast<size_t>(written);
        }
        return true;
    }

    bool load(const clap_istream_t* stream, uint32_t context) noexcept
    {
        if (!stream) return false;
        State state {};
        auto* bytes = reinterpret_cast<uint8_t*>(&state);
        for (size_t done = 0; done < sizeof(state);)
        {
            const auto read = stream->read(stream, bytes + done, sizeof(state) - done);
            if (read <= 0) return false;
            done += static_cast<size_t>(read);
        }
        // Anything after the state means it is not ours.
        uint8_t extra = 0;
        if (stream->read(stream, &extra, 1) > 0) return false;
        if (state.magic != stateMagic || state.version != stateVersion
            || (state.savedFor != plainState && !knownContext(state.savedFor)))
            return false;
        loadedSavedFor.store(static_cast<int32_t>(state.savedFor), std::memory_order_relaxed);
        loadedWith.store(static_cast<int32_t>(context), std::memory_order_relaxed);
        return true;
    }

    std::atomic<int32_t> loadedSavedFor { -1 };
    std::atomic<int32_t> loadedWith { -1 };
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
    return (new StateContextPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                    nullptr };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP State Context Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Writes the context its last loaded state was saved for, and the context it was loaded "
        "with, to its output",
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

} // namespace wclap_test_plugins::state_context

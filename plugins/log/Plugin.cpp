#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::log
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;

struct Message
{
    clap_log_severity severity;
    const char* text;
};

constexpr Message audioThreadMessage { CLAP_LOG_DEBUG, "log test 1/7: debug, audio thread" };
constexpr Message mainThreadMessages[] {
    { CLAP_LOG_INFO, "log test 2/7: info, main thread, UTF-8 \xc3\xa9\xe2\x9c\x93" },
    { CLAP_LOG_WARNING, "log test 3/7: warning, main thread" },
    { CLAP_LOG_ERROR, "log test 4/7: error, main thread" },
    { CLAP_LOG_FATAL, "log test 5/7: fatal, main thread" },
    { CLAP_LOG_HOST_MISBEHAVING, "log test 6/7: host misbehaving, main thread" },
    { CLAP_LOG_PLUGIN_MISBEHAVING, "log test 7/7: plug-in misbehaving, main thread" },
};

class LogPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit LogPlugin(const clap_host_t* host) : Base(&descriptor(), host), host(host) {}

protected:
    bool init() noexcept override
    {
        hostLog = static_cast<const clap_host_log_t*>(host->get_extension(host, CLAP_EXT_LOG));
        return true;
    }

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

        // The first block logs once from the audio thread and asks for the
        // main thread to log the rest.
        if (!started.exchange(true, std::memory_order_relaxed))
        {
            send(audioThreadMessage);
            host->request_callback(host);
        }
        std::fill_n(left, process->frames_count,
                    static_cast<float>(sent.load(std::memory_order_relaxed)));
        std::fill_n(right, process->frames_count,
                    static_cast<float>(lastSeverity.load(std::memory_order_relaxed)));
        output.constant_mask = 0b11;
        return CLAP_PROCESS_CONTINUE;
    }

    void onMainThread() noexcept override
    {
        if (!started.load(std::memory_order_relaxed) || mainThreadDone) return;
        mainThreadDone = true;
        for (const auto& message : mainThreadMessages) send(message);
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
        std::snprintf(info->name, sizeof(info->name), "Messages sent / last severity");
        return true;
    }

private:
    void send(const Message& message) noexcept
    {
        if (!hostLog) return;
        hostLog->log(host, message.severity, message.text);
        sent.fetch_add(1, std::memory_order_relaxed);
        lastSeverity.store(message.severity, std::memory_order_relaxed);
    }

    const clap_host_t* host;
    const clap_host_log_t* hostLog = nullptr;
    std::atomic<bool> started { false };
    bool mainThreadDone = false;
    std::atomic<uint32_t> sent { 0 };
    std::atomic<int32_t> lastSeverity { -1 };
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
    return (new LogPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                    nullptr };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Log Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Logs one message at every severity, from the audio and main threads, and writes how many "
        "it sent to its output",
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

} // namespace wclap_test_plugins::log

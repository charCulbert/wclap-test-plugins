#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/log.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 16;
constexpr char pluginId[] = TEST_PLUGIN_ID;

int callbackRequests = 0;
std::vector<std::pair<clap_log_severity, std::string>> logged;

void hostLogMessage(const clap_host_t*, clap_log_severity severity, const char* message)
{
    logged.emplace_back(severity, message);
}
const clap_host_log_t hostLog { hostLogMessage };

const void* hostGetExtension(const clap_host_t*, const char* id)
{
    return std::strcmp(id, CLAP_EXT_LOG) == 0 ? &hostLog : nullptr;
}
void hostRequest(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) { ++callbackRequests; }

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

// Processes one block and checks both channels hold the expected values throughout.
void expectOutput(const clap_plugin_t* plugin, float sent, float severity, const char* what)
{
    std::array<float, blockSize> left {}, right {};
    left.fill(-9.0f);
    right.fill(-9.0f);
    std::array<float*, 2> channels { left.data(), right.data() };
    clap_audio_buffer_t output { channels.data(), nullptr, 2, 0, 0 };
    clap_process_t process {};
    process.frames_count = blockSize;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            std::string(what) + ": status mismatch");
    for (uint32_t frame = 0; frame < blockSize; ++frame)
        require(left[frame] == sent && right[frame] == severity,
                std::string(what) + ": frame " + std::to_string(frame) + " is "
                    + std::to_string(left[frame]) + ", " + std::to_string(right[frame]));
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "log-smoke", "wclap-test-plugins", "", "0.1.0",
        hostGetExtension, hostRequest, hostRequest, hostRequestCallback
    };

    require(clap_entry.init("."), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "factory mismatch");
    const auto* plugin = factory->create_plugin(factory, &host, pluginId);
    require(plugin && plugin->init(plugin), "plugin init failed");

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    clap_audio_port_info_t audioInfo {};
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 1
                && audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 2,
            "audio port mismatch");

    plugin->on_main_thread(plugin);
    require(logged.empty(), "logged before processing");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");
    expectOutput(plugin, 1.0f, CLAP_LOG_DEBUG, "after the audio-thread message");
    require(callbackRequests == 1, "request_callback count is " + std::to_string(callbackRequests));
    expectOutput(plugin, 1.0f, CLAP_LOG_DEBUG, "second block logs nothing");
    require(callbackRequests == 1, "second block requested a callback");

    plugin->on_main_thread(plugin);
    plugin->on_main_thread(plugin);
    expectOutput(plugin, 7.0f, CLAP_LOG_PLUGIN_MISBEHAVING, "after the main-thread messages");

    const std::vector<std::pair<clap_log_severity, std::string>> expected {
        { CLAP_LOG_DEBUG, "log test 1/7: debug, audio thread" },
        { CLAP_LOG_INFO, "log test 2/7: info, main thread, UTF-8 \xc3\xa9\xe2\x9c\x93" },
        { CLAP_LOG_WARNING, "log test 3/7: warning, main thread" },
        { CLAP_LOG_ERROR, "log test 4/7: error, main thread" },
        { CLAP_LOG_FATAL, "log test 5/7: fatal, main thread" },
        { CLAP_LOG_HOST_MISBEHAVING, "log test 6/7: host misbehaving, main thread" },
        { CLAP_LOG_PLUGIN_MISBEHAVING, "log test 7/7: plug-in misbehaving, main thread" },
    };
    require(logged == expected,
            "logged " + std::to_string(logged.size()) + " messages, not the 7 expected");

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
        std::puts("log native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "log native smoke failed: %s\n", error.what());
        return 1;
    }
}

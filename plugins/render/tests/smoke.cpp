#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/render.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 64;
constexpr char pluginId[] = TEST_PLUGIN_ID;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

// Processes one block and checks every frame holds `expected`.
void expectBlock(const clap_plugin_t* plugin, float expected, const char* what)
{
    std::array<float, blockSize> samples {};
    samples.fill(-9.0f);
    float* channels[] { samples.data() };
    clap_audio_buffer_t output { channels, nullptr, 1, 0, 0 };
    clap_process_t process {};
    process.frames_count = blockSize;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            std::string(what) + ": status mismatch");
    for (uint32_t frame = 0; frame < blockSize; ++frame)
        require(samples[frame] == expected,
                std::string(what) + ": frame " + std::to_string(frame) + " is "
                    + std::to_string(samples[frame]) + ", expected " + std::to_string(expected));
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "render-smoke", "wclap-test-plugins", "", "0.1.0",
        hostGetExtension, hostRequest, hostRequest, hostRequest
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
                && audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 1,
            "audio port mismatch");
    const auto* render = static_cast<const clap_plugin_render_t*>(
        plugin->get_extension(plugin, CLAP_EXT_RENDER));
    require(render && !render->has_hard_realtime_requirement(plugin),
            "render missing or claims a hard real-time requirement");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");
    expectBlock(plugin, 1.0f, "default real time");

    require(render->set(plugin, CLAP_RENDER_OFFLINE), "set offline failed");
    expectBlock(plugin, 2.0f, "offline");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "reactivate failed");
    require(plugin->start_processing(plugin), "restart processing failed");
    expectBlock(plugin, 2.0f, "offline kept across reactivation");

    require(render->set(plugin, CLAP_RENDER_REALTIME), "set real time failed");
    expectBlock(plugin, 1.0f, "back to real time");

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
        std::puts("render native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "render native smoke failed: %s\n", error.what());
        return 1;
    }
}

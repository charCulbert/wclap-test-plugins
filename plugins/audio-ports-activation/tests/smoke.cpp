#include "Plugin.h"

#include <clap/ext/audio-ports-activation.h>
#include <clap/ext/audio-ports.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 16;
constexpr char pluginId[] = TEST_PLUGIN_ID;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

// Activates, processes one block, deactivates, and checks each output holds
// its expected value with constant_mask set.
void expectOutputs(const clap_plugin_t* plugin, float first, float second, const char* what)
{
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize),
            std::string(what) + ": activate failed");
    require(plugin->start_processing(plugin), std::string(what) + ": start processing failed");
    std::array<std::array<float, blockSize>, 2> samples {};
    for (auto& channel : samples) channel.fill(-9.0f);
    std::array<float*, 1> firstChannels { samples[0].data() };
    std::array<float*, 1> secondChannels { samples[1].data() };
    std::array<clap_audio_buffer_t, 2> outputs { {
        { firstChannels.data(), nullptr, 1, 0, 0 },
        { secondChannels.data(), nullptr, 1, 0, 0 },
    } };
    clap_process_t process {};
    process.frames_count = blockSize;
    process.audio_outputs = outputs.data();
    process.audio_outputs_count = 2;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            std::string(what) + ": status mismatch");
    const float expected[] { first, second };
    for (uint32_t port = 0; port < 2; ++port)
    {
        require(outputs[port].constant_mask == 1,
                std::string(what) + ": constant_mask not set on output " + std::to_string(port));
        for (uint32_t frame = 0; frame < blockSize; ++frame)
            require(samples[port][frame] == expected[port],
                    std::string(what) + ": output " + std::to_string(port) + " frame "
                        + std::to_string(frame) + " is " + std::to_string(samples[port][frame]));
    }
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "audio-ports-activation-smoke", "wclap-test-plugins", "", "0.1.0",
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
    const auto* activation = static_cast<const clap_plugin_audio_ports_activation_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_ACTIVATION));
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 2,
            "audio port count mismatch");
    for (uint32_t index = 0; index < 2; ++index)
    {
        clap_audio_port_info_t info {};
        require(audioPorts->get(plugin, index, false, &info) && info.channel_count == 1,
                "output is not mono");
    }
    require(activation && !activation->can_activate_while_processing(plugin),
            "activation missing or allows changes while processing");

    expectOutputs(plugin, 1.0f, 2.0f, "both active");
    require(activation->set_active(plugin, false, 1, false, 32), "deactivate output 2 failed");
    expectOutputs(plugin, 1.0f, 0.0f, "output 2 inactive");
    require(activation->set_active(plugin, false, 0, false, 0), "deactivate output 1 failed");
    expectOutputs(plugin, 0.0f, 0.0f, "both inactive");

    require(!activation->set_active(plugin, false, 2, true, 32), "accepted port index 2");
    require(!activation->set_active(plugin, true, 0, true, 32), "accepted an input port");
    require(!activation->set_active(plugin, false, 0, true, 64), "accepted 64-bit buffers");
    expectOutputs(plugin, 0.0f, 0.0f, "refused calls change nothing");

    require(activation->set_active(plugin, false, 1, true, 32), "reactivate output 2 failed");
    expectOutputs(plugin, 0.0f, 2.0f, "output 2 active again");

    // Refused while the plug-in is active.
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(!activation->set_active(plugin, false, 0, true, 32), "accepted a change while active");
    plugin->deactivate(plugin);
    expectOutputs(plugin, 0.0f, 2.0f, "change while active refused");

    plugin->destroy(plugin);
    clap_entry.deinit();
}

} // namespace

int main()
{
    try
    {
        runSmoke();
        std::puts("audio-ports-activation native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "audio-ports-activation native smoke failed: %s\n", error.what());
        return 1;
    }
}

#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/param-indication.h>
#include <clap/ext/params.h>
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
constexpr clap_id targetParamId = 1;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

// Processes one block and checks the four channels (mapping, its colour,
// automation state, its colour) hold the expected values throughout.
void expectOutput(const clap_plugin_t* plugin, const std::array<float, 4>& expected,
                  const char* what)
{
    std::array<std::array<float, blockSize>, 4> samples {};
    for (auto& channel : samples) channel.fill(-9.0f);
    std::array<float*, 2> mappingChannels { samples[0].data(), samples[1].data() };
    std::array<float*, 2> automationChannels { samples[2].data(), samples[3].data() };
    std::array<clap_audio_buffer_t, 2> outputs { {
        { mappingChannels.data(), nullptr, 2, 0, 0 },
        { automationChannels.data(), nullptr, 2, 0, 0 },
    } };
    clap_process_t process {};
    process.frames_count = blockSize;
    process.audio_outputs = outputs.data();
    process.audio_outputs_count = 2;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            std::string(what) + ": status mismatch");
    for (uint32_t channel = 0; channel < 4; ++channel)
        for (uint32_t frame = 0; frame < blockSize; ++frame)
            require(samples[channel][frame] == expected[channel],
                    std::string(what) + ": channel " + std::to_string(channel) + " frame "
                        + std::to_string(frame) + " is "
                        + std::to_string(samples[channel][frame]) + ", expected "
                        + std::to_string(expected[channel]));
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "param-indication-smoke", "wclap-test-plugins", "", "0.1.0",
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
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* indication = static_cast<const clap_plugin_param_indication_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAM_INDICATION));
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 2,
            "audio port count mismatch");
    for (uint32_t index = 0; index < 2; ++index)
    {
        clap_audio_port_info_t info {};
        require(audioPorts->get(plugin, index, false, &info) && info.channel_count == 2,
                "output is not stereo");
    }
    clap_param_info_t paramInfo {};
    require(params && params->count(plugin) == 1 && params->get_info(plugin, 0, &paramInfo)
                && paramInfo.id == targetParamId,
            "parameter info mismatch");
    require(indication != nullptr, "no param-indication extension");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");
    expectOutput(plugin, { -1, -1, -1, -1 }, "nothing indicated");

    const clap_color_t orange { 255, 255, 128, 0 };
    const clap_color_t blue { 128, 0, 0, 255 };
    indication->set_mapping(plugin, targetParamId, true, &orange, "Knob 1", "Cutoff");
    expectOutput(plugin, { 1, 0xff8000, -1, -1 }, "mapped with a colour");
    indication->set_automation(plugin, targetParamId, CLAP_PARAM_INDICATION_AUTOMATION_RECORDING,
                               &blue);
    expectOutput(plugin, { 1, 0xff8000, CLAP_PARAM_INDICATION_AUTOMATION_RECORDING, 0x0000ff },
                 "recording automation");
    indication->set_mapping(plugin, targetParamId, true, nullptr, nullptr, nullptr);
    expectOutput(plugin, { 1, -1, CLAP_PARAM_INDICATION_AUTOMATION_RECORDING, 0x0000ff },
                 "mapped without a colour");
    // Alpha 0 shows nothing, so it reads as no colour.
    const clap_color_t clearRed { 0, 255, 0, 0 };
    indication->set_mapping(plugin, targetParamId, true, &CLAP_COLOR_TRANSPARENT, nullptr,
                            nullptr);
    indication->set_automation(plugin, targetParamId, CLAP_PARAM_INDICATION_AUTOMATION_PLAYING,
                               &clearRed);
    expectOutput(plugin, { 1, -1, CLAP_PARAM_INDICATION_AUTOMATION_PLAYING, -1 },
                 "transparent colours");
    indication->set_automation(plugin, targetParamId, CLAP_PARAM_INDICATION_AUTOMATION_NONE,
                               nullptr);
    indication->set_mapping(plugin, targetParamId, false, nullptr, nullptr, nullptr);
    expectOutput(plugin, { 0, -1, CLAP_PARAM_INDICATION_AUTOMATION_NONE, -1 }, "cleared");

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
        std::puts("param-indication native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "param-indication native smoke failed: %s\n", error.what());
        return 1;
    }
}

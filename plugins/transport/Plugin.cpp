#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::transport
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;

// One value per output channel, in port order: port p holds channels 2p and
// 2p + 1. Channel 0 (the event count) is not part of the held state.
enum Channel : uint32_t
{
    eventCount,
    flags,
    songPosBeats,
    songPosSeconds,
    tempo,
    tempoInc,
    barStart,
    barNumber,
    tsigNum,
    tsigDenom,
    loopStartBeats,
    loopEndBeats,
    loopStartSeconds,
    loopEndSeconds,
    channelCount
};

constexpr uint32_t portCount = channelCount / 2;

const char* const portNames[portCount] {
    "Transport events / flags", "Song position beats / seconds", "Tempo / tempo increment",
    "Bar start / bar number",   "Time signature",                "Loop beats",
    "Loop seconds",
};

using Values = std::array<double, channelCount>;

double beats(clap_beattime value) noexcept
{
    return static_cast<double>(value) / static_cast<double>(CLAP_BEATTIME_FACTOR);
}

double seconds(clap_sectime value) noexcept
{
    return static_cast<double>(value) / static_cast<double>(CLAP_SECTIME_FACTOR);
}

// No transport: every held channel reads -1.
Values none() noexcept
{
    Values values;
    values.fill(-1.0);
    values[eventCount] = 0.0;
    return values;
}

// A field is read only when its flag says it is valid (events.h); otherwise it
// reads 0. The loop points belong to the beats or seconds timeline.
Values read(const clap_event_transport_t& transport) noexcept
{
    Values values {};
    const auto has = [&](uint32_t flag) { return (transport.flags & flag) != 0; };
    values[flags] = transport.flags;
    if (has(CLAP_TRANSPORT_HAS_BEATS_TIMELINE))
    {
        values[songPosBeats] = beats(transport.song_pos_beats);
        values[barStart] = beats(transport.bar_start);
        values[barNumber] = transport.bar_number;
        values[loopStartBeats] = beats(transport.loop_start_beats);
        values[loopEndBeats] = beats(transport.loop_end_beats);
    }
    if (has(CLAP_TRANSPORT_HAS_SECONDS_TIMELINE))
    {
        values[songPosSeconds] = seconds(transport.song_pos_seconds);
        values[loopStartSeconds] = seconds(transport.loop_start_seconds);
        values[loopEndSeconds] = seconds(transport.loop_end_seconds);
    }
    if (has(CLAP_TRANSPORT_HAS_TEMPO))
    {
        values[tempo] = transport.tempo;
        values[tempoInc] = transport.tempo_inc;
    }
    if (has(CLAP_TRANSPORT_HAS_TIME_SIGNATURE))
    {
        values[tsigNum] = transport.tsig_num;
        values[tsigDenom] = transport.tsig_denom;
    }
    return values;
}

class TransportPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit TransportPlugin(const clap_host_t* host) : Base(&descriptor(), host) {}

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override { return true; }
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != portCount) return CLAP_PROCESS_ERROR;
        std::array<float*, channelCount> out {};
        for (uint32_t port = 0; port < portCount; ++port)
        {
            auto& output = process->audio_outputs[port];
            if (output.channel_count != 2 || !output.data32) return CLAP_PROCESS_ERROR;
            for (uint32_t channel = 0; channel < 2; ++channel)
            {
                out[2 * port + channel] = output.data32[channel];
                if (!out[2 * port + channel]) return CLAP_PROCESS_ERROR;
            }
            output.constant_mask = 0;
        }

        // The block's transport holds from frame 0; each transport event takes
        // over from its own frame, in list order.
        auto values = process->transport ? read(*process->transport) : none();
        const auto* events = process->in_events;
        const auto count = events ? events->size(events) : 0;
        uint32_t next = 0;
        const auto frames = process->frames_count;
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            values[eventCount] = 0.0;
            // Events stamped past the block are applied on its last frame.
            for (; next < count; ++next)
            {
                const auto* header = events->get(events, next);
                if (!header) continue;
                if (std::min(header->time, frames - 1) > frame) break;
                if (header->space_id != CLAP_CORE_EVENT_SPACE_ID
                    || header->type != CLAP_EVENT_TRANSPORT
                    || header->size < sizeof(clap_event_transport_t))
                    continue;
                const auto held = values[eventCount] + 1.0;
                values = read(*reinterpret_cast<const clap_event_transport_t*>(header));
                values[eventCount] = held;
            }
            for (uint32_t channel = 0; channel < channelCount; ++channel)
                out[channel][frame] = static_cast<float>(values[channel]);
        }
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        return isInput ? 0 : portCount;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (isInput || index >= portCount || !info) return false;
        *info = {};
        info->id = index;
        info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "%s", portNames[index]);
        return true;
    }
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
    return (new TransportPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] {
        CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr
    };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Transport Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Writes the host's transport into audio, sample-accurately, from the block's transport "
        "and each transport event on its frame",
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

} // namespace wclap_test_plugins::transport

#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::midi_echo
{
namespace
{

// One bundle, three plug-ins that differ only in the one dialect their note
// ports list. Each takes, marks and echoes whatever dialect arrives.
constexpr char midiEchoId[] = TEST_PLUGIN_ID;
constexpr char midi2EchoId[] = TEST_PLUGIN_ID ".midi2";
constexpr char clapEchoId[] = TEST_PLUGIN_ID ".clap";

struct Dialects
{
    uint32_t supported;
    clap_note_dialect preferred;
};

constexpr Dialects midiDialects { CLAP_NOTE_DIALECT_MIDI, CLAP_NOTE_DIALECT_MIDI };
constexpr Dialects midi2Dialects { CLAP_NOTE_DIALECT_MIDI2, CLAP_NOTE_DIALECT_MIDI2 };
constexpr Dialects clapDialects { CLAP_NOTE_DIALECT_CLAP, CLAP_NOTE_DIALECT_CLAP };

const char* features[] { CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr };

const clap_plugin_descriptor_t descriptors[] {
    { CLAP_VERSION, midiEchoId, "WCLAP MIDI Echo Test", "Charlie Culbert", TEST_PLUGIN_URL,
      TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
      "Echoes every note and MIDI event and marks its type and flags in audio; its note ports "
      "list the MIDI dialect only",
      features },
    { CLAP_VERSION, midi2EchoId, "WCLAP MIDI 2 Echo Test", "Charlie Culbert", TEST_PLUGIN_URL,
      TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
      "Echoes every note and MIDI event and marks its type and flags in audio; its note ports "
      "list the MIDI 2 dialect only",
      features },
    { CLAP_VERSION, clapEchoId, "WCLAP CLAP Note Echo Test", "Charlie Culbert", TEST_PLUGIN_URL,
      TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
      "Echoes every note and MIDI event and marks its type and flags in audio; its note ports "
      "list the CLAP dialect only",
      features },
};

constexpr uint32_t descriptorCount = sizeof(descriptors) / sizeof(descriptors[0]);

class EchoPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    EchoPlugin(const clap_plugin_descriptor_t* descriptor, const clap_host_t* host,
               Dialects dialects)
        : Base(descriptor, host), dialects(dialects)
    {
    }

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
        const auto frames = process->frames_count;
        std::fill_n(left, frames, 0.0f);
        std::fill_n(right, frames, 0.0f);

        const auto* in = process->in_events;
        const auto* out = process->out_events;
        const auto count = in ? in->size(in) : 0;
        for (uint32_t index = 0; index < count; ++index)
        {
            const auto* header = in->get(in, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->time >= frames)
                continue;
            // Type plus 1, so CLAP_EVENT_NOTE_ON (0) differs from no event.
            left[header->time] = header->type + 1.0f;
            right[header->time] = header->flags;
            switch (header->type)
            {
                case CLAP_EVENT_NOTE_ON:
                case CLAP_EVENT_NOTE_OFF:
                case CLAP_EVENT_NOTE_CHOKE:
                case CLAP_EVENT_MIDI:
                case CLAP_EVENT_MIDI_SYSEX:
                case CLAP_EVENT_MIDI2:
                    // events.h: the host copies the event, and a sysex event's
                    // buffer, inside try_push.
                    if (out) out->try_push(out, header);
                    break;
                default:
                    break;
            }
        }
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
        std::snprintf(info->name, sizeof(info->name), "Event type / flags");
        return true;
    }

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool) const noexcept override { return 1; }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info_t* info) const noexcept override
    {
        if (index != 0 || !info) return false;
        *info = {};
        info->id = 0;
        info->supported_dialects = dialects.supported;
        info->preferred_dialect = dialects.preferred;
        std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Events in" : "Echo");
        return true;
    }

private:
    const Dialects dialects;
};

uint32_t pluginCount(const clap_plugin_factory_t*) { return descriptorCount; }

const clap_plugin_descriptor_t* pluginDescriptor(const clap_plugin_factory_t*,
                                                 uint32_t index)
{
    return index < descriptorCount ? &descriptors[index] : nullptr;
}

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
                                  const clap_host_t* host, const char* id)
{
    if (!host || !id) return nullptr;
    if (std::strcmp(id, midiEchoId) == 0)
        return (new EchoPlugin(&descriptors[0], host, midiDialects))->clapPlugin();
    if (std::strcmp(id, midi2EchoId) == 0)
        return (new EchoPlugin(&descriptors[1], host, midi2Dialects))->clapPlugin();
    if (std::strcmp(id, clapEchoId) == 0)
        return (new EchoPlugin(&descriptors[2], host, clapDialects))->clapPlugin();
    return nullptr;
}

} // namespace

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

} // namespace wclap_test_plugins::midi_echo

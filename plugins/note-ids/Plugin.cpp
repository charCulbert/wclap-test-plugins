#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::note_ids
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr size_t maxVoices = 64;

struct Voice
{
    bool active = false;
    uint64_t started = 0; // start order, for NOTE_END order
    int32_t noteId = -1;
    int16_t port = -1;
    int16_t channel = -1;
    int16_t key = -1;
};

// A note ID n >= 0 is written as n + 1, so ID 0 still makes a click; -1 is
// written as -1.
float encode(int32_t noteId) noexcept
{
    return noteId < 0 ? -1.0f : static_cast<float>(noteId) + 1.0f;
}

class NoteIdsPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit NoteIdsPlugin(const clap_host_t* host) : Base(&descriptor(), host) {}

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override
    {
        voices = {};
        return true;
    }
    bool startProcessing() noexcept override { return true; }
    void reset() noexcept override { voices = {}; }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != 1) return CLAP_PROCESS_ERROR;
        auto& output = process->audio_outputs[0];
        if (output.channel_count != 2 || !output.data32) return CLAP_PROCESS_ERROR;
        auto* started = output.data32[0];
        auto* ended = output.data32[1];
        if (!started || !ended) return CLAP_PROCESS_ERROR;

        const auto frames = process->frames_count;
        std::fill_n(started, frames, 0.0f);
        std::fill_n(ended, frames, 0.0f);
        if (frames == 0) return CLAP_PROCESS_CONTINUE;

        const auto* events = process->in_events;
        const auto count = events ? events->size(events) : 0;
        for (uint32_t index = 0; index < count; ++index)
        {
            const auto* header = events->get(events, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
                || header->size < sizeof(clap_event_note_t))
                continue;
            // Events stamped past the block are applied on its last frame.
            const auto frame = std::min(header->time, frames - 1);
            const auto& note = *reinterpret_cast<const clap_event_note_t*>(header);
            if (header->type == CLAP_EVENT_NOTE_ON)
                started[frame] += noteOn(note, frame, process->out_events, ended[frame]);
            else if (header->type == CLAP_EVENT_NOTE_OFF || header->type == CLAP_EVENT_NOTE_CHOKE)
                ended[frame] += endVoices(note, frame, process->out_events);
        }
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        return isInput ? 0u : 1u;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (isInput || index != 0 || !info) return false;
        *info = {};
        info->id = 0x4e494430;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "Started / ended note IDs");
        return true;
    }

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        return isInput ? 1u : 0u;
    }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info_t* info) const noexcept override
    {
        if (!isInput || index != 0 || !info) return false;
        *info = {};
        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf(info->name, sizeof(info->name), "Notes");
        return true;
    }

private:
    float noteOn(const clap_event_note_t& note, uint32_t frame, const clap_output_events_t* out,
                 float& ended) noexcept
    {
        if (note.port_index < 0 || note.channel < 0 || note.key < 0) return 0.0f;
        const Voice voice { true, ++startCount, note.note_id, note.port_index, note.channel,
                            note.key };
        const auto free = std::find_if(voices.begin(), voices.end(),
                                       [](const Voice& voice) { return !voice.active; });
        if (free != voices.end())
            *free = voice;
        else
        {
            // The host keeps a voice for every note-on until NOTE_END
            // (events.h), so a note with no voice ends at once.
            ended += encode(voice.noteId);
            sendNoteEnd(voice, frame, out);
        }
        return encode(note.note_id);
    }

    // Ends every voice the event matches (-1 in a field matches any value),
    // sends NOTE_END for each with the address its note-on gave, and returns
    // the sum of their encoded IDs.
    float endVoices(const clap_event_note_t& note, uint32_t frame,
                    const clap_output_events_t* out) noexcept
    {
        std::array<Voice*, maxVoices> matched {};
        size_t count = 0;
        for (auto& voice : voices)
        {
            if (!voice.active || (note.note_id != -1 && note.note_id != voice.noteId)
                || (note.port_index != -1 && note.port_index != voice.port)
                || (note.channel != -1 && note.channel != voice.channel)
                || (note.key != -1 && note.key != voice.key))
                continue;
            matched[count++] = &voice;
        }
        // In the order the voices started, whichever slots they use.
        std::sort(matched.begin(), matched.begin() + count,
                  [](const Voice* a, const Voice* b) { return a->started < b->started; });
        float sum = 0.0f;
        for (size_t index = 0; index < count; ++index)
        {
            sum += encode(matched[index]->noteId);
            sendNoteEnd(*matched[index], frame, out);
            *matched[index] = {};
        }
        return sum;
    }

    static void sendNoteEnd(const Voice& voice, uint32_t frame,
                            const clap_output_events_t* out) noexcept
    {
        if (!out) return;
        clap_event_note_t end {};
        end.header = { sizeof(end), frame, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END, 0 };
        end.note_id = voice.noteId;
        end.port_index = voice.port;
        end.channel = voice.channel;
        end.key = voice.key;
        out->try_push(out, &end.header);
    }

    std::array<Voice, maxVoices> voices {};
    uint64_t startCount = 0;
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
    return (new NoteIdsPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] {
        CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr
    };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Note IDs Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Reports the note ID of every note it starts and ends", features
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

} // namespace wclap_test_plugins::note_ids

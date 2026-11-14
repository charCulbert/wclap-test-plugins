#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wclap_test_plugins::probe
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr clap_id levelParamId = 1;
constexpr double tuningScale = 1.0 / 128.0;
constexpr size_t maxVoices = 64;

struct Voice
{
    bool active = false;
    uint64_t started = 0; // start order, for NOTE_END order
    int32_t noteId = -1;
    int16_t port = -1;
    int16_t channel = -1;
    int16_t key = -1;
    double modulation = 0.0;
    double tuning = 0.0;
};

// CLAP addresses voices by (note_id, port, channel, key); -1 in an event
// field matches any value.
bool matches(const Voice& voice, int32_t noteId, int16_t port, int16_t channel,
             int16_t key) noexcept
{
    return voice.active && (noteId == -1 || noteId == voice.noteId)
        && (port == -1 || port == voice.port) && (channel == -1 || channel == voice.channel)
        && (key == -1 || key == voice.key);
}

template <typename Event>
const Event* as(const clap_event_header_t* header) noexcept
{
    return header->size >= sizeof(Event) ? reinterpret_cast<const Event*>(header) : nullptr;
}

class ProbePlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit ProbePlugin(const clap_host_t* host) : Base(&descriptor(), host) {}

protected:
    bool activate(double, uint32_t, uint32_t) noexcept override
    {
        clear();
        return true;
    }
    bool startProcessing() noexcept override { return true; }
    void reset() noexcept override { clear(); }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != 1) return CLAP_PROCESS_ERROR;
        auto& output = process->audio_outputs[0];
        if (output.channel_count != 2 || !output.data32) return CLAP_PROCESS_ERROR;
        auto* left = output.data32[0];
        auto* right = output.data32[1];
        if (!left || !right) return CLAP_PROCESS_ERROR;

        const auto* events = process->in_events;
        const auto count = events ? events->size(events) : 0;
        uint32_t next = 0;
        const auto frames = process->frames_count;
        outEvents = process->out_events;
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            eventFrame = frame;
            double click = 0.0;
            // Events stamped past the block are applied on its last frame.
            for (; next < count; ++next)
            {
                const auto* header = events->get(events, next);
                if (!header) continue;
                if (std::min(header->time, frames - 1) > frame) break;
                click += apply(header);
            }
            left[frame] = static_cast<float>(voiceSum + click);
            right[frame] = static_cast<float>(level.load(std::memory_order_relaxed)
                                              + globalModulation);
        }
        outEvents = nullptr;
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool) const noexcept override { return 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (index != 0 || !info) return false;
        *info = {};
        info->id = isInput ? 0x50524930 : 0x50524f30;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "%s",
                      isInput ? "Ignored input" : "Clicks and voices / parameter");
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
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf(info->name, sizeof(info->name), "Notes");
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return 1; }
    bool paramsInfo(uint32_t index, clap_param_info_t* info) const noexcept override
    {
        if (index != 0 || !info) return false;
        *info = {};
        info->id = levelParamId;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE
            | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY
            | CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT
            | CLAP_PARAM_REQUIRES_PROCESS;
        std::snprintf(info->name, sizeof(info->name), "Level");
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    }

    bool paramsValue(clap_id paramId, double* value) noexcept override
    {
        if (paramId != levelParamId || !value) return false;
        *value = level.load(std::memory_order_relaxed);
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char* display,
                           uint32_t size) noexcept override
    {
        if (paramId != levelParamId || !display || size == 0) return false;
        std::snprintf(display, size, "%.6f", value);
        return true;
    }

    bool paramsTextToValue(clap_id paramId, const char* display,
                           double* value) noexcept override
    {
        if (paramId != levelParamId || !display || !value) return false;
        char* end = nullptr;
        *value = std::strtod(display, &end);
        return end != display;
    }

    void paramsFlush(const clap_input_events_t* in, const clap_output_events_t*) noexcept override
    {
        const auto count = in ? in->size(in) : 0;
        for (uint32_t index = 0; index < count; ++index)
        {
            const auto* header = in->get(in, index);
            if (header && header->space_id == CLAP_CORE_EVENT_SPACE_ID
                && (header->type == CLAP_EVENT_PARAM_VALUE || header->type == CLAP_EVENT_PARAM_MOD))
                apply(header);
        }
    }

private:
    void clear() noexcept
    {
        voices = {};
        voiceSum = 0.0;
        globalModulation = 0.0;
    }

    // Applies one event and returns the click it writes on its frame.
    double apply(const clap_event_header_t* header) noexcept
    {
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return 0.0;
        switch (header->type)
        {
            case CLAP_EVENT_NOTE_ON:
                if (const auto* event = as<clap_event_note_t>(header))
                    return noteOn(event->note_id, event->port_index, event->channel, event->key);
                break;
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE:
                if (const auto* event = as<clap_event_note_t>(header))
                    return noteOff(event->note_id, event->port_index, event->channel, event->key);
                break;
            case CLAP_EVENT_MIDI:
                if (const auto* event = as<clap_event_midi_t>(header))
                {
                    const auto status = event->data[0] & 0xf0;
                    const auto channel = static_cast<int16_t>(event->data[0] & 0x0f);
                    const auto key = static_cast<int16_t>(event->data[1] & 0x7f);
                    const auto port = static_cast<int16_t>(event->port_index);
                    if (status == 0x90 && event->data[2] != 0)
                        return noteOn(-1, port, channel, key);
                    if (status == 0x80 || status == 0x90)
                        return noteOff(-1, port, channel, key);
                }
                break;
            case CLAP_EVENT_PARAM_VALUE:
                if (const auto* event = as<clap_event_param_value_t>(header))
                {
                    // Per-note automation is not advertised, so a value
                    // addressed to a voice is ignored.
                    if (event->param_id == levelParamId && event->note_id == -1
                        && event->port_index == -1 && event->channel == -1 && event->key == -1)
                        level.store(event->value, std::memory_order_relaxed);
                }
                break;
            case CLAP_EVENT_PARAM_MOD:
                if (const auto* event = as<clap_event_param_mod_t>(header))
                {
                    if (event->param_id != levelParamId) break;
                    if (event->note_id == -1 && event->port_index == -1 && event->channel == -1
                        && event->key == -1)
                    {
                        globalModulation = event->amount;
                        break;
                    }
                    for (auto& voice : voices)
                        if (matches(voice, event->note_id, event->port_index, event->channel,
                                    event->key))
                            voice.modulation = event->amount;
                    updateVoiceSum();
                }
                break;
            case CLAP_EVENT_NOTE_EXPRESSION:
                if (const auto* event = as<clap_event_note_expression_t>(header))
                {
                    if (event->expression_id != CLAP_NOTE_EXPRESSION_TUNING) break;
                    for (auto& voice : voices)
                        if (matches(voice, event->note_id, event->port_index, event->channel,
                                    event->key))
                            voice.tuning = event->value;
                    updateVoiceSum();
                }
                break;
            default:
                break;
        }
        return 0.0;
    }

    double noteOn(int32_t noteId, int16_t port, int16_t channel, int16_t key) noexcept
    {
        if (port < 0 || channel < 0 || key < 0) return 0.0;
        const auto free = std::find_if(voices.begin(), voices.end(),
                                       [](const Voice& voice) { return !voice.active; });
        const Voice voice { true, ++startCount, noteId, port, channel, key, 0.0, 0.0 };
        if (free == voices.end())
        {
            // The host keeps a voice for every note-on until NOTE_END
            // (events.h), so a note with no voice ends at once.
            sendNoteEnd(voice);
            return 1.0;
        }
        *free = voice;
        updateVoiceSum();
        return 1.0;
    }

    // Ends every matching voice and sends NOTE_END for each with the address
    // its note-on gave, in the order the voices started.
    double noteOff(int32_t noteId, int16_t port, int16_t channel, int16_t key) noexcept
    {
        std::array<Voice*, maxVoices> ended {};
        size_t count = 0;
        for (auto& voice : voices)
            if (matches(voice, noteId, port, channel, key)) ended[count++] = &voice;
        std::sort(ended.begin(), ended.begin() + count,
                  [](const Voice* a, const Voice* b) { return a->started < b->started; });
        for (size_t index = 0; index < count; ++index)
        {
            sendNoteEnd(*ended[index]);
            *ended[index] = {};
        }
        updateVoiceSum();
        return -1.0;
    }

    void sendNoteEnd(const Voice& voice) noexcept
    {
        if (!outEvents) return;
        clap_event_note_t end {};
        end.header = { sizeof(end), eventFrame, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END,
                       0 };
        end.note_id = voice.noteId;
        end.port_index = voice.port;
        end.channel = voice.channel;
        end.key = voice.key;
        outEvents->try_push(outEvents, &end.header);
    }

    void updateVoiceSum() noexcept
    {
        voiceSum = 0.0;
        for (const auto& voice : voices)
            if (voice.active) voiceSum += voice.modulation + voice.tuning * tuningScale;
    }

    std::atomic<double> level { 0.0 };
    std::array<Voice, maxVoices> voices {};
    uint64_t startCount = 0;
    double voiceSum = 0.0;
    double globalModulation = 0.0;
    const clap_output_events_t* outEvents = nullptr;
    uint32_t eventFrame = 0;
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
    return (new ProbePlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] {
        CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr
    };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Event Probe Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Writes note, parameter, modulation and expression events to audio on their frames",
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

} // namespace wclap_test_plugins::probe

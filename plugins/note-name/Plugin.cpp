#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::note_name
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr std::array<int16_t, 4> keys { 36, 38, 42, 46 };
constexpr std::array<const char*, 4> names { "Kick", "Snare", "Closed hi-hat", "Open hi-hat" };
// Kit 1 renames key 38 only.
constexpr char kit1Key38[] = "Rim";

class NoteNamePlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit NoteNamePlugin(const clap_host_t* host) : Base(&descriptor(), host), host(host) {}

protected:
    bool init() noexcept override
    {
        hostNoteName = static_cast<const clap_host_note_name_t*>(
            host->get_extension(host, CLAP_EXT_NOTE_NAME));
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

        const auto* events = process->in_events;
        const auto count = events ? events->size(events) : 0;
        uint32_t next = 0;
        const auto frames = process->frames_count;
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            // Events stamped past the block are applied on its last frame.
            for (; next < count; ++next)
            {
                const auto* header = events->get(events, next);
                if (!header) continue;
                if (std::min(header->time, frames - 1) > frame) break;
                apply(header);
            }
            left[frame] = static_cast<float>(kit.load(std::memory_order_relaxed));
            right[frame] = static_cast<float>(reads.load(std::memory_order_relaxed));
        }
        return CLAP_PROCESS_CONTINUE;
    }

    void onMainThread() noexcept override
    {
        if (changePending.exchange(false, std::memory_order_acq_rel) && hostNoteName)
            hostNoteName->changed(host);
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
        std::snprintf(info->name, sizeof(info->name), "Kit / note name reads");
        return true;
    }

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override { return isInput ? 1 : 0; }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info_t* info) const noexcept override
    {
        if (!isInput || index != 0 || !info) return false;
        *info = {};
        info->id = 0;
        // Program change exists only in MIDI, and the plug-in plays no notes.
        info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
        std::snprintf(info->name, sizeof(info->name), "Named notes");
        return true;
    }

    bool implementsNoteName() const noexcept override { return true; }
    uint32_t noteNameCount() noexcept override { return keys.size() + 1; }
    // Calls for index 0 stand in for the host's reads of the list: a host that
    // reads the whole list starts there. (count() is not counted: clap-helpers
    // calls it from get().)
    bool noteNameGet(uint32_t index, clap_note_name_t* noteName) noexcept override
    {
        if (!noteName || index > keys.size()) return false;
        if (index == 0) reads.fetch_add(1, std::memory_order_relaxed);
        *noteName = {};
        noteName->port = 0;
        noteName->channel = -1;
        if (index == keys.size())
        {
            noteName->key = -1;
            std::snprintf(noteName->name, sizeof(noteName->name), "Any key");
            return true;
        }
        noteName->key = keys[index];
        const auto* name = kit.load(std::memory_order_relaxed) == 1 && keys[index] == 38
            ? kit1Key38 : names[index];
        std::snprintf(noteName->name, sizeof(noteName->name), "%s", name);
        return true;
    }

private:
    // A MIDI program change selects kit 0 (program 0) or kit 1 (any other).
    void apply(const clap_event_header_t* header) noexcept
    {
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_MIDI
            || header->size < sizeof(clap_event_midi_t))
            return;
        const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
        if ((event->data[0] & 0xf0) != 0xc0) return;
        const int32_t next = event->data[1] == 0 ? 0 : 1;
        if (kit.exchange(next, std::memory_order_relaxed) == next) return;
        if (!changePending.exchange(true, std::memory_order_acq_rel))
            host->request_callback(host);
    }

    const clap_host_t* host;
    const clap_host_note_name_t* hostNoteName = nullptr;
    std::atomic<int32_t> kit { 0 };
    std::atomic<uint32_t> reads { 0 };
    std::atomic<bool> changePending { false };
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
    return (new NoteNamePlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                    nullptr };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Note Name Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Names drum keys, renames one on a program change, and counts the host's note name reads",
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

} // namespace wclap_test_plugins::note_name

#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wclap_test_plugins::remote_controls
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr std::array<const char*, 4> paramNames { "A", "B", "C", "D" };
constexpr clap_id mainPageId = 1;
constexpr clap_id presetPageId = 2;

class RemoteControlsPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit RemoteControlsPlugin(const clap_host_t* host)
        : Base(&descriptor(), host), host(host)
    {
    }

protected:
    bool init() noexcept override
    {
        hostRemoteControls = static_cast<const clap_host_remote_controls_t*>(
            host->get_extension(host, CLAP_EXT_REMOTE_CONTROLS));
        if (!hostRemoteControls)
            hostRemoteControls = static_cast<const clap_host_remote_controls_t*>(
                host->get_extension(host, CLAP_EXT_REMOTE_CONTROLS_COMPAT));
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
            left[frame] = static_cast<float>(lastParam);
            right[frame] = static_cast<float>(reads.load(std::memory_order_relaxed));
        }
        return CLAP_PROCESS_CONTINUE;
    }

    void onMainThread() noexcept override
    {
        if (!changePending.exchange(false, std::memory_order_acq_rel) || !hostRemoteControls)
            return;
        hostRemoteControls->changed(host);
        hostRemoteControls->suggest_page(
            host, layout.load(std::memory_order_relaxed) == 0 ? mainPageId : presetPageId);
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
        std::snprintf(info->name, sizeof(info->name), "Last parameter / page reads");
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
        std::snprintf(info->name, sizeof(info->name), "Program change");
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return paramNames.size(); }
    bool paramsInfo(uint32_t index, clap_param_info_t* info) const noexcept override
    {
        if (!info || index >= paramNames.size()) return false;
        *info = {};
        info->id = index;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        std::snprintf(info->name, sizeof(info->name), "%s", paramNames[index]);
        return true;
    }
    bool paramsValue(clap_id id, double* value) noexcept override
    {
        if (!value || id >= values.size()) return false;
        *value = values[id].load(std::memory_order_relaxed);
        return true;
    }
    bool paramsValueToText(clap_id id, double value, char* display,
                           uint32_t size) noexcept override
    {
        if (id >= values.size() || !display || size == 0) return false;
        std::snprintf(display, size, "%.6f", value);
        return true;
    }
    bool paramsTextToValue(clap_id id, const char* display, double* value) noexcept override
    {
        if (id >= values.size() || !display || !value) return false;
        char* end = nullptr;
        *value = std::strtod(display, &end);
        return end != display;
    }
    void paramsFlush(const clap_input_events_t* in, const clap_output_events_t*) noexcept override
    {
        // params.h: flush delivers parameter events only. A program change
        // found here is not one, so it is ignored.
        const auto count = in ? in->size(in) : 0;
        for (uint32_t index = 0; index < count; ++index)
        {
            const auto* header = in->get(in, index);
            if (header && header->type == CLAP_EVENT_PARAM_VALUE) apply(header);
        }
    }

    bool implementRemoteControls() const noexcept override { return true; }
    uint32_t remoteControlsPageCount() noexcept override { return 2; }
    // Calls for index 0 stand in for the host's reads of the pages: a host
    // that reads them all starts there.
    bool remoteControlsPageGet(uint32_t index,
                               clap_remote_controls_page_t* page) noexcept override
    {
        if (!page || index >= 2) return false;
        *page = {};
        std::fill(std::begin(page->param_ids), std::end(page->param_ids), CLAP_INVALID_ID);
        std::snprintf(page->section_name, sizeof(page->section_name), "Controls");
        if (index == 0)
        {
            reads.fetch_add(1, std::memory_order_relaxed);
            const auto reversed = layout.load(std::memory_order_relaxed) == 1;
            page->page_id = mainPageId;
            page->is_for_preset = false;
            std::snprintf(page->page_name, sizeof(page->page_name), "Main");
            for (clap_id slot = 0; slot < paramNames.size(); ++slot)
                page->param_ids[slot] = reversed ? paramNames.size() - 1 - slot : slot;
        }
        else
        {
            page->page_id = presetPageId;
            page->is_for_preset = true;
            std::snprintf(page->page_name, sizeof(page->page_name), "Preset");
            page->param_ids[0] = 2;
            page->param_ids[1] = 3;
        }
        return true;
    }

private:
    void apply(const clap_event_header_t* header) noexcept
    {
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        if (header->type == CLAP_EVENT_PARAM_VALUE
            && header->size >= sizeof(clap_event_param_value_t))
        {
            const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
            if (event->param_id >= values.size()) return;
            values[event->param_id].store(event->value, std::memory_order_relaxed);
            lastParam = event->param_id + 1;
        }
        else if (header->type == CLAP_EVENT_MIDI && header->size >= sizeof(clap_event_midi_t))
        {
            // A program change selects layout 0 (program 0) or 1 (any other).
            const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
            if ((event->data[0] & 0xf0) != 0xc0) return;
            const int32_t next = event->data[1] == 0 ? 0 : 1;
            if (layout.exchange(next, std::memory_order_relaxed) == next) return;
            if (!changePending.exchange(true, std::memory_order_acq_rel))
                host->request_callback(host);
        }
    }

    const clap_host_t* host;
    const clap_host_remote_controls_t* hostRemoteControls = nullptr;
    std::array<std::atomic<double>, 4> values {};
    std::atomic<int32_t> layout { 0 };
    std::atomic<uint32_t> reads { 0 };
    std::atomic<bool> changePending { false };
    uint32_t lastParam = 0;
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
    return (new RemoteControlsPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY,
                                    nullptr };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Remote Controls Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Maps four parameters to remote-control pages, remaps them on a program change, and "
        "writes the last parameter the host set and its page reads to its output",
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

} // namespace wclap_test_plugins::remote_controls

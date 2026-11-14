#include "Plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace wclap_test_plugins::process_lifecycle
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr uint32_t finiteTailSamples = 256;
constexpr uint32_t infiniteTailSamples = 0x7fffffffU;
constexpr double steadyTimeScale = 65'536.0;

enum class Mode
{
    sleep,
    finiteTail,
    quietAware,
    continuous,
    infiniteTail,
    error
};

class ProcessLifecyclePlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit ProcessLifecyclePlugin(const clap_host_t* host)
        : Base(&descriptor(), host), host(host) {}

protected:
    bool init() noexcept override
    {
        // clap-helpers does not fetch clap_host_tail, so fetch it here.
        const auto* tail = static_cast<const clap_host_tail_t*>(
            host->get_extension(host, CLAP_EXT_TAIL));
        hostTail = tail && tail->changed ? tail : nullptr;
        return true;
    }

    // Our own clap.tail: CLAP allows tail.get on the main thread whether or not
    // the plug-in is active (ext/tail.h), and the helpers' version reports a
    // call while inactive as host misbehaviour.
    const void* extension(const char* id) noexcept override
    {
        if (id && std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExtension;
        return nullptr;
    }

    bool activate(double, uint32_t, uint32_t) noexcept override
    {
        // Each activation runs the protocol from the start.
        firstProcess = true;
        quietAwareAudible = false;
        previousSteadyTime = -1;
        previousFrames = 0;
        mode.store(Mode::sleep, std::memory_order_relaxed);
        mainThreadWakePending.store(false, std::memory_order_relaxed);
        requestedWakeProof.store(false, std::memory_order_relaxed);
        reportedTail = tailGet();
        return true;
    }
    bool startProcessing() noexcept override { return true; }

    // plugin.h: after reset, steady_time may jump backward, so the comparison
    // starts over.
    void reset() noexcept override
    {
        previousSteadyTime = -1;
        previousFrames = 0;
    }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_outputs_count != 2) return CLAP_PROCESS_ERROR;
        auto* signal = monoOutput(process->audio_outputs[0]);
        auto* steady = monoOutput(process->audio_outputs[1]);
        if (!signal || !steady) return CLAP_PROCESS_ERROR;

        std::fill_n(signal, process->frames_count, 0.0f);
        std::fill_n(steady, process->frames_count, 0.0f);
        const bool modeChanged = applyEvents(process->in_events, process->out_events);
        steadyMark = steadyTimeAdvance(process);
        reportTailChange();

        if (firstProcess)
        {
            firstProcess = false;
            fill(signal, process->frames_count, 0.1f);
            fill(steady, process->frames_count, steadyMark);
            mainThreadWakePending.store(true, std::memory_order_release);
            host->request_callback(host);
            return CLAP_PROCESS_SLEEP;
        }
        // A note-on in the woken block sets the mode, and the mode's marker wins.
        if (requestedWakeProof.exchange(false, std::memory_order_acq_rel) && !modeChanged)
        {
            fill(signal, process->frames_count, 0.2f);
            fill(steady, process->frames_count, steadyMark);
            return CLAP_PROCESS_SLEEP;
        }

        switch (mode.load(std::memory_order_relaxed))
        {
            case Mode::finiteTail:
                fill(signal, process->frames_count, 0.3f);
                fill(steady, process->frames_count, steadyMark);
                return CLAP_PROCESS_TAIL;
            case Mode::quietAware:
                if (quietAwareAudible)
                {
                    quietAwareAudible = false;
                    fill(signal, process->frames_count, 0.4f);
                    fill(steady, process->frames_count, steadyMark);
                }
                // Both outputs deliberately remain zero on the following call so a
                // host can apply CONTINUE_IF_NOT_QUIET to the complete output set.
                return CLAP_PROCESS_CONTINUE_IF_NOT_QUIET;
            case Mode::continuous:
                fill(signal, process->frames_count, 0.5f);
                fill(steady, process->frames_count, steadyMark);
                return CLAP_PROCESS_CONTINUE;
            case Mode::infiniteTail:
                fill(signal, process->frames_count, 0.6f);
                fill(steady, process->frames_count, steadyMark);
                return CLAP_PROCESS_TAIL;
            case Mode::sleep:
                fill(steady, process->frames_count, steadyMark);
                return CLAP_PROCESS_SLEEP;
            case Mode::error:
                // A conforming host must discard these conspicuous values.
                fill(signal, process->frames_count, 9.0f);
                fill(steady, process->frames_count, 9.0f);
                return CLAP_PROCESS_ERROR;
        }
        return CLAP_PROCESS_ERROR;
    }

    void onMainThread() noexcept override
    {
        if (!mainThreadWakePending.exchange(false, std::memory_order_acq_rel)) return;
        requestedWakeProof.store(true, std::memory_order_release);
        host->request_process(host);
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        return isInput ? 0u : 2u;
    }

    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (isInput || index >= 2 || !info) return false;
        *info = {};
        info->id = 0x50524330 + index;
        info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
        info->channel_count = 1;
        info->port_type = CLAP_PORT_MONO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "%s",
                      index == 0 ? "Status signal" : "Steady time advance / 65536");
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
        info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
        std::snprintf(info->name, sizeof(info->name), "Lifecycle controls");
        return true;
    }

    bool implementsTail() const noexcept override { return true; }
    uint32_t tailGet() const noexcept override
    {
        switch (mode.load(std::memory_order_relaxed))
        {
            case Mode::finiteTail: return finiteTailSamples;
            case Mode::infiniteTail: return infiniteTailSamples;
            default: return 0;
        }
    }

private:
    static float* monoOutput(clap_audio_buffer_t& output) noexcept
    {
        return output.channel_count == 1 && output.data32 ? output.data32[0] : nullptr;
    }

    static void fill(float* output, uint32_t frames, float value) noexcept
    {
        std::fill_n(output, frames, value);
    }

    // How far steady_time moved since the previous process call, over 65536.
    // process.h: steady_time is -1 or >= 0, and grows by at least the previous
    // call's frames_count. 0 when there is nothing to compare (-1, or the first
    // call); -1 when the rule is broken.
    float steadyTimeAdvance(const clap_process_t* process) noexcept
    {
        const auto now = process->steady_time;
        const auto before = previousSteadyTime;
        const auto frames = previousFrames;
        previousSteadyTime = now;
        previousFrames = process->frames_count;
        if (now < -1) return -1.0f;
        if (now == -1 || before < 0) return 0.0f;
        const auto advance = now - before;
        if (advance < static_cast<int64_t>(frames)) return -1.0f;
        return static_cast<float>(
            std::min(static_cast<double>(advance), steadyTimeScale) / steadyTimeScale);
    }

    // ext/tail.h: tell the host whenever the tail changes.
    void reportTailChange() noexcept
    {
        const auto tail = tailGet();
        if (tail == reportedTail) return;
        reportedTail = tail;
        if (hostTail) hostTail->changed(host);
    }

    static uint32_t clapTailGet(const clap_plugin_t* plugin) noexcept
    {
        const auto* base = static_cast<const Base*>(plugin->plugin_data);
        return static_cast<const ProcessLifecyclePlugin*>(base)->tailGet();
    }

    // Returns whether the key selects a mode.
    bool setModeForKey(uint8_t key) noexcept
    {
        switch (key)
        {
            case 60: mode.store(Mode::finiteTail, std::memory_order_relaxed); break;
            case 61:
                mode.store(Mode::quietAware, std::memory_order_relaxed);
                quietAwareAudible = true;
                break;
            case 62: mode.store(Mode::sleep, std::memory_order_relaxed); break;
            case 63: mode.store(Mode::continuous, std::memory_order_relaxed); break;
            case 64: mode.store(Mode::infiniteTail, std::memory_order_relaxed); break;
            case 65: mode.store(Mode::error, std::memory_order_relaxed); break;
            default: return false;
        }
        return true;
    }

    // Returns whether a note-on selected a mode.
    bool applyEvents(const clap_input_events_t* events,
                     const clap_output_events_t* out) noexcept
    {
        bool modeChanged = false;
        const auto count = events ? events->size(events) : 0;
        for (uint32_t index = 0; index < count; ++index)
        {
            const auto* header = events->get(events, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            if (header->type == CLAP_EVENT_MIDI
                && header->size >= sizeof(clap_event_midi_t))
            {
                const auto& event = *reinterpret_cast<const clap_event_midi_t*>(header);
                if ((event.data[0] & 0xf0) == 0x90 && event.data[2] != 0)
                    modeChanged |= setModeForKey(event.data[1]);
            }
            else if (header->type == CLAP_EVENT_NOTE_ON
                     && header->size >= sizeof(clap_event_note_t))
            {
                const auto& event = *reinterpret_cast<const clap_event_note_t*>(header);
                if (event.key >= 0 && event.key <= 127)
                    modeChanged |= setModeForKey(static_cast<uint8_t>(event.key));
                // events.h: the host keeps a voice for each note-on until the
                // plug-in sends NOTE_END. No note sounds, so end it at once.
                if (out)
                {
                    auto end = event;
                    end.header.type = CLAP_EVENT_NOTE_END;
                    end.header.size = sizeof(end);
                    end.header.flags = 0;
                    out->try_push(out, &end.header);
                }
            }
        }
        return modeChanged;
    }

    const clap_host_t* host = nullptr;
    const clap_plugin_tail_t tailExtension { clapTailGet };
    const clap_host_tail_t* hostTail = nullptr;
    uint32_t reportedTail = 0;
    std::atomic<bool> mainThreadWakePending { false };
    std::atomic<bool> requestedWakeProof { false };
    std::atomic<Mode> mode { Mode::sleep };
    bool firstProcess = true;
    bool quietAwareAudible = false;
    int64_t previousSteadyTime = -1;
    uint32_t previousFrames = 0;
    float steadyMark = 0.0f;
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
    return (new ProcessLifecyclePlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] {
        CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_ANALYZER, nullptr
    };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP Process Lifecycle Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Observable CLAP process status, wake, steady-time, and tail fixture", features
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

} // namespace wclap_test_plugins::process_lifecycle

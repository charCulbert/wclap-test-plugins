#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/remote-controls.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 32;
constexpr char pluginId[] = TEST_PLUGIN_ID;

int callbackRequests = 0;
int pagesChanged = 0;
std::vector<clap_id> suggested;

void hostChanged(const clap_host_t*) { ++pagesChanged; }
void hostSuggestPage(const clap_host_t*, clap_id pageId) { suggested.push_back(pageId); }
const clap_host_remote_controls_t hostRemoteControls { hostChanged, hostSuggestPage };

// The extension ID the host answers to: the final one, or the draft one an
// older host has.
const char* hostRemoteControlsId = CLAP_EXT_REMOTE_CONTROLS;

const void* hostGetExtension(const clap_host_t*, const char* id)
{
    return std::strcmp(id, hostRemoteControlsId) == 0 ? &hostRemoteControls : nullptr;
}
void hostRequest(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) { ++callbackRequests; }

union AnyEvent
{
    clap_event_header_t header;
    clap_event_midi_t midi;
    clap_event_param_value_t value;
};

struct EventList
{
    std::vector<AnyEvent> list;
    clap_input_events_t events { this, size, get };

    static uint32_t size(const clap_input_events_t* events)
    {
        return static_cast<uint32_t>(static_cast<const EventList*>(events->ctx)->list.size());
    }

    static const clap_event_header_t* get(const clap_input_events_t* events, uint32_t index)
    {
        const auto& list = static_cast<const EventList*>(events->ctx)->list;
        return index < list.size() ? &list[index].header : nullptr;
    }

    void program(uint32_t time, uint8_t program)
    {
        AnyEvent event {};
        event.midi = { { sizeof(clap_event_midi_t), time, CLAP_CORE_EVENT_SPACE_ID,
                         CLAP_EVENT_MIDI, 0 },
                       0, { 0xc0, program, 0 } };
        list.push_back(event);
    }

    void value(uint32_t time, clap_id id, double value)
    {
        AnyEvent event {};
        event.value = { { sizeof(clap_event_param_value_t), time, CLAP_CORE_EVENT_SPACE_ID,
                          CLAP_EVENT_PARAM_VALUE, 0 },
                        id, nullptr, -1, -1, -1, -1, value };
        list.push_back(event);
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Block
{
    std::array<float, blockSize> left {};
    std::array<float, blockSize> right {};
    std::array<float*, 2> channels { left.data(), right.data() };
    clap_audio_buffer_t output { channels.data(), nullptr, 2, 0, 0 };

    void run(const clap_plugin_t* plugin, EventList& events)
    {
        left.fill(-9.0f);
        right.fill(-9.0f);
        clap_process_t process {};
        process.frames_count = blockSize;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1;
        process.in_events = &events.events;
        require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE, "status mismatch");
    }
};

void expect(const std::array<float, blockSize>& samples, uint32_t from, uint32_t to,
            float expected, const char* what)
{
    for (auto frame = from; frame < to; ++frame)
        require(samples[frame] == expected,
                std::string(what) + ": frame " + std::to_string(frame) + " is "
                    + std::to_string(samples[frame]) + ", expected "
                    + std::to_string(expected));
}

// Reads every page as a host would: "id:name:preset:params;" with unused slots left out.
std::string readPages(const clap_plugin_t* plugin, const clap_plugin_remote_controls_t* controls)
{
    std::string pages;
    const auto count = controls->count(plugin);
    for (uint32_t index = 0; index < count; ++index)
    {
        clap_remote_controls_page_t page {};
        require(controls->get(plugin, index, &page), "page get failed");
        require(std::strcmp(page.section_name, "Controls") == 0, "section name mismatch");
        pages += std::to_string(page.page_id) + ":" + page.page_name + ":"
            + (page.is_for_preset ? "preset" : "device") + ":";
        for (const auto id : page.param_ids)
            if (id != CLAP_INVALID_ID) pages += std::to_string(id);
        pages += ";";
    }
    return pages;
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "remote-controls-smoke", "wclap-test-plugins", "", "0.1.0",
        hostGetExtension, hostRequest, hostRequest, hostRequestCallback
    };

    require(clap_entry.init("."), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "factory mismatch");
    const auto* plugin = factory->create_plugin(factory, &host, pluginId);
    require(plugin && plugin->init(plugin), "plugin init failed");

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* controls = static_cast<const clap_plugin_remote_controls_t*>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS));
    clap_audio_port_info_t audioInfo {};
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 1
                && audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 2,
            "audio port mismatch");
    clap_note_port_info_t noteInfo {};
    require(notePorts && notePorts->count(plugin, true) == 1
                && notePorts->get(plugin, 0, true, &noteInfo)
                && noteInfo.supported_dialects == CLAP_NOTE_DIALECT_MIDI
                && noteInfo.preferred_dialect == CLAP_NOTE_DIALECT_MIDI,
            "note port mismatch");
    require(params && params->count(plugin) == 4, "parameter count mismatch");
    for (uint32_t index = 0; index < 4; ++index)
    {
        clap_param_info_t info {};
        require(params->get_info(plugin, index, &info) && info.id == index
                    && (info.flags & CLAP_PARAM_IS_AUTOMATABLE),
                "parameter info mismatch");
    }
    require(controls != nullptr, "no remote-controls extension");
    require(readPages(plugin, controls) == "1:Main:device:0123;2:Preset:preset:23;",
            "layout 0 pages mismatch");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");

    // The host turns knob 1 of page Main: parameter 1 in layout 0.
    EventList events;
    events.value(4, 1, 0.25);
    events.program(8, 0);  // already layout 0: no change
    events.program(12, 3); // layout 1
    Block block;
    block.run(plugin, events);
    expect(block.left, 0, 4, 0.0f, "no parameter set yet");
    expect(block.left, 4, blockSize, 2.0f, "parameter 1 set");
    expect(block.right, 0, blockSize, 1.0f, "one page read");
    require(callbackRequests == 1, "request_callback count is " + std::to_string(callbackRequests));

    plugin->on_main_thread(plugin);
    require(pagesChanged == 1 && suggested == std::vector<clap_id> { 2 },
            "changed and suggest_page(2) not called once");
    plugin->on_main_thread(plugin);
    require(pagesChanged == 1, "changed called again with no change");
    require(readPages(plugin, controls) == "1:Main:device:3210;2:Preset:preset:23;",
            "layout 1 pages mismatch");

    // Knob 1 of page Main is now parameter 2.
    EventList knob;
    knob.value(6, 2, 0.75);
    knob.program(10, 0);
    block.run(plugin, knob);
    expect(block.left, 0, 6, 2.0f, "parameter 1 still last");
    expect(block.left, 6, blockSize, 3.0f, "parameter 2 set");
    expect(block.right, 0, blockSize, 2.0f, "two page reads");
    plugin->on_main_thread(plugin);
    require(pagesChanged == 2 && suggested == std::vector<clap_id> { 2, 1 },
            "changed and suggest_page(1) not called for the change back");

    double value = -1.0;
    require(params->get_value(plugin, 1, &value) && value == 0.25, "parameter 1 value mismatch");
    require(params->get_value(plugin, 2, &value) && value == 0.75, "parameter 2 value mismatch");

    // params.flush applies parameter values and ignores a program change.
    plugin->stop_processing(plugin);
    EventList flush;
    flush.value(0, 3, 0.5);
    flush.program(0, 1);
    params->flush(plugin, &flush.events, nullptr);
    require(params->get_value(plugin, 3, &value) && value == 0.5, "flush did not set parameter 3");
    require(callbackRequests == 2, "flush applied a program change");
    require(readPages(plugin, controls) == "1:Main:device:0123;2:Preset:preset:23;",
            "flush changed the layout");

    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    // A host that only has the draft extension ID still gets changed.
    hostRemoteControlsId = CLAP_EXT_REMOTE_CONTROLS_COMPAT;
    plugin = factory->create_plugin(factory, &host, pluginId);
    require(plugin && plugin->init(plugin), "plugin init failed with the draft host extension");
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize)
                && plugin->start_processing(plugin),
            "activate failed with the draft host extension");
    EventList draft;
    draft.program(0, 1);
    block.run(plugin, draft);
    plugin->on_main_thread(plugin);
    require(pagesChanged == 3, "changed not called through the draft host extension");
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
        std::puts("remote-controls native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "remote-controls native smoke failed: %s\n", error.what());
        return 1;
    }
}

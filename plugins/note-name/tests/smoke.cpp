#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
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
int namesChanged = 0;

void hostNoteNameChanged(const clap_host_t*) { ++namesChanged; }
const clap_host_note_name_t hostNoteName { hostNoteNameChanged };

const void* hostGetExtension(const clap_host_t*, const char* id)
{
    return std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0 ? &hostNoteName : nullptr;
}
void hostRequest(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) { ++callbackRequests; }

struct EventList
{
    std::vector<clap_event_midi_t> list;
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

    void midi(uint32_t time, uint8_t status, uint8_t data1)
    {
        list.push_back({ { sizeof(clap_event_midi_t), time, CLAP_CORE_EVENT_SPACE_ID,
                           CLAP_EVENT_MIDI, 0 },
                         0, { status, data1, 0 } });
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

// Reads every note name as a host would and returns them as "key=name" lines.
std::string readNames(const clap_plugin_t* plugin, const clap_plugin_note_name_t* noteName)
{
    std::string names;
    const auto count = noteName->count(plugin);
    for (uint32_t index = 0; index < count; ++index)
    {
        clap_note_name_t name {};
        require(noteName->get(plugin, index, &name), "get failed");
        require(name.port == 0 && name.channel == -1, "name port or channel mismatch");
        names += std::to_string(name.key) + "=" + name.name + ";";
    }
    return names;
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "note-name-smoke", "wclap-test-plugins", "", "0.1.0",
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
    const auto* noteName = static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME));
    clap_audio_port_info_t audioInfo {};
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 1
                && audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 2,
            "audio port mismatch");
    clap_note_port_info_t noteInfo {};
    require(notePorts && notePorts->count(plugin, true) == 1
                && notePorts->count(plugin, false) == 0
                && notePorts->get(plugin, 0, true, &noteInfo)
                && noteInfo.supported_dialects == CLAP_NOTE_DIALECT_MIDI
                && noteInfo.preferred_dialect == CLAP_NOTE_DIALECT_MIDI,
            "note port mismatch");
    require(noteName != nullptr, "no note-name extension");

    const std::string kit0 = "36=Kick;38=Snare;42=Closed hi-hat;46=Open hi-hat;-1=Any key;";
    const std::string kit1 = "36=Kick;38=Rim;42=Closed hi-hat;46=Open hi-hat;-1=Any key;";
    require(readNames(plugin, noteName) == kit0, "kit 0 names mismatch");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");

    EventList events;
    events.midi(4, 0xc0, 0);  // already kit 0: no change
    events.midi(8, 0xc0, 1);  // kit 1, as in the README
    events.midi(20, 0xc3, 5); // any channel, still kit 1
    Block block;
    block.run(plugin, events);
    expect(block.left, 0, 8, 0.0f, "kit 0");
    expect(block.left, 8, blockSize, 1.0f, "kit 1 from the program change");
    expect(block.right, 0, blockSize, 1.0f, "one read so far");
    require(callbackRequests == 1, "request_callback count is " + std::to_string(callbackRequests));

    plugin->on_main_thread(plugin);
    require(namesChanged == 1, "note_name.changed not called once");
    plugin->on_main_thread(plugin);
    require(namesChanged == 1, "note_name.changed called again with no change");
    require(readNames(plugin, noteName) == kit1, "kit 1 names mismatch");

    EventList back;
    back.midi(4, 0xc0, 0);
    block.run(plugin, back);
    expect(block.left, 0, 4, 1.0f, "kit 1 before the change back");
    expect(block.left, 4, blockSize, 0.0f, "kit 0 again");
    expect(block.right, 0, blockSize, 2.0f, "two reads");
    require(callbackRequests == 2, "no request_callback for the change back");
    plugin->on_main_thread(plugin);
    require(namesChanged == 2, "note_name.changed not called for the change back");

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
        std::puts("note-name native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "note-name native smoke failed: %s\n", error.what());
        return 1;
    }
}

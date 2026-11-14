#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 128;
constexpr char pluginId[] = TEST_PLUGIN_ID;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

struct EventList
{
    std::vector<clap_event_note_t> list;
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

    void add(uint32_t time, uint16_t type, int32_t noteId, int16_t key, int16_t channel = 0,
             int16_t port = 0)
    {
        list.push_back({ { sizeof(clap_event_note_t), time, CLAP_CORE_EVENT_SPACE_ID, type, 0 },
                         noteId, port, channel, key, 1.0 });
    }
};

struct OutputList
{
    std::vector<clap_event_note_t> ends;
    clap_output_events_t events { this, push };

    static bool push(const clap_output_events_t* events, const clap_event_header_t* header)
    {
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_NOTE_END
            || header->size != sizeof(clap_event_note_t))
            throw std::runtime_error("unexpected output event");
        static_cast<OutputList*>(events->ctx)->ends.push_back(
            *reinterpret_cast<const clap_event_note_t*>(header));
        return true;
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "note-ids-smoke", "wclap-test-plugins", "", "0.1.0",
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
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    clap_audio_port_info_t audioInfo {};
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 1
                && audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 2,
            "audio port mismatch");
    clap_note_port_info_t noteInfo {};
    require(notePorts && notePorts->count(plugin, true) == 1
                && notePorts->count(plugin, false) == 0
                && notePorts->get(plugin, 0, true, &noteInfo)
                && noteInfo.supported_dialects == CLAP_NOTE_DIALECT_CLAP,
            "note port mismatch");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");

    EventList in;
    in.add(2, CLAP_EVENT_NOTE_ON, 0, 60);
    in.add(4, CLAP_EVENT_NOTE_ON, 100, 60);
    in.add(6, CLAP_EVENT_NOTE_ON, 200, 62, 1);
    in.add(8, CLAP_EVENT_NOTE_ON, -1, 64);
    in.add(10, CLAP_EVENT_NOTE_OFF, 100, 60);
    in.add(12, CLAP_EVENT_NOTE_OFF, 999, 62, 1);
    in.add(14, CLAP_EVENT_NOTE_OFF, -1, 62, 0);
    in.add(16, CLAP_EVENT_NOTE_CHOKE, 200, 62, 1);
    in.add(18, CLAP_EVENT_NOTE_ON, 300, 60);
    in.add(20, CLAP_EVENT_NOTE_OFF, -1, 60);
    in.add(22, CLAP_EVENT_NOTE_OFF, -1, -1, -1, -1);
    in.add(24, CLAP_EVENT_NOTE_ON, 400, -1);
    in.add(500, CLAP_EVENT_NOTE_ON, 5, 65);

    std::array<float, blockSize> started {};
    std::array<float, blockSize> ended {};
    started.fill(-9.0f);
    ended.fill(-9.0f);
    std::array<float*, 2> channels { started.data(), ended.data() };
    clap_audio_buffer_t output { channels.data(), nullptr, 2, 0, 0 };
    OutputList out;
    clap_process_t process {};
    process.frames_count = blockSize;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &in.events;
    process.out_events = &out.events;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE, "status mismatch");

    const std::map<uint32_t, float> expectedStarted {
        { 2, 1.0f }, { 4, 101.0f }, { 6, 201.0f }, { 8, -1.0f }, { 18, 301.0f }, { 127, 6.0f }
    };
    const std::map<uint32_t, float> expectedEnded {
        { 10, 101.0f }, { 16, 201.0f }, { 20, 302.0f }, { 22, -1.0f }
    };
    for (uint32_t frame = 0; frame < blockSize; ++frame)
    {
        const auto findOr = [frame](const std::map<uint32_t, float>& map) {
            const auto found = map.find(frame);
            return found == map.end() ? 0.0f : found->second;
        };
        require(started[frame] == findOr(expectedStarted),
                "left at frame " + std::to_string(frame) + " is "
                    + std::to_string(started[frame]));
        require(ended[frame] == findOr(expectedEnded),
                "right at frame " + std::to_string(frame) + " is "
                    + std::to_string(ended[frame]));
    }

    struct End { uint32_t time; int32_t noteId; int16_t channel; int16_t key; };
    const std::vector<End> expectedEnds {
        { 10, 100, 0, 60 }, { 16, 200, 1, 62 }, { 20, 0, 0, 60 }, { 20, 300, 0, 60 },
        { 22, -1, 0, 64 }
    };
    require(out.ends.size() == expectedEnds.size(),
            "NOTE_END count is " + std::to_string(out.ends.size()));
    for (size_t index = 0; index < expectedEnds.size(); ++index)
    {
        const auto& actual = out.ends[index];
        const auto& expected = expectedEnds[index];
        require(actual.header.time == expected.time && actual.note_id == expected.noteId
                    && actual.port_index == 0 && actual.channel == expected.channel
                    && actual.key == expected.key,
                "NOTE_END " + std::to_string(index) + " mismatch: time "
                    + std::to_string(actual.header.time) + ", note ID "
                    + std::to_string(actual.note_id) + ", key " + std::to_string(actual.key));
    }

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
        std::puts("note-ids native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "note-ids native smoke failed: %s\n", error.what());
        return 1;
    }
}

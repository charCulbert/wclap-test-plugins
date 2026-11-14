#include "Plugin.h"

#include <clap/ext/audio-ports.h>
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
constexpr char midiEchoId[] = TEST_PLUGIN_ID;
constexpr char midi2EchoId[] = TEST_PLUGIN_ID ".midi2";
constexpr char clapEchoId[] = TEST_PLUGIN_ID ".clap";
const uint8_t sysexBytes[] { 0xf0, 0x7d, 0x01, 0x02, 0xf7 };

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

union AnyEvent
{
    clap_event_header_t header;
    clap_event_note_t note;
    clap_event_midi_t midi;
    clap_event_midi_sysex_t sysex;
    clap_event_midi2_t midi2;
    clap_event_param_value_t value;
};

clap_event_header_t header(uint32_t time, uint16_t type, uint32_t size, uint32_t flags = 0)
{
    return { size, time, CLAP_CORE_EVENT_SPACE_ID, type, flags };
}

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

    void add(const AnyEvent& event) { list.push_back(event); }
};

struct OutputList
{
    std::vector<AnyEvent> list;
    clap_output_events_t events { this, push };

    static bool push(const clap_output_events_t* events, const clap_event_header_t* header)
    {
        if (header->size > sizeof(AnyEvent)) throw std::runtime_error("output event too large");
        AnyEvent copy {};
        std::memcpy(&copy, header, header->size);
        static_cast<OutputList*>(events->ctx)->list.push_back(copy);
        return true;
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

const clap_host_t host {
    CLAP_VERSION, nullptr, "midi-echo-smoke", "wclap-test-plugins", "", "0.1.0",
    hostGetExtension, hostRequest, hostRequest, hostRequest
};

void check(const clap_plugin_factory_t* factory, const char* id, uint32_t supported,
           uint32_t preferred)
{
    const std::string name = id;
    const auto* plugin = factory->create_plugin(factory, &host, id);
    require(plugin && plugin->init(plugin), name + ": init failed");

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    clap_audio_port_info_t audioInfo {};
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 1
                && audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 2,
            name + ": audio port mismatch");
    for (const bool isInput : { true, false })
    {
        clap_note_port_info_t noteInfo {};
        require(notePorts && notePorts->count(plugin, isInput) == 1
                    && notePorts->get(plugin, 0, isInput, &noteInfo)
                    && noteInfo.supported_dialects == supported
                    && noteInfo.preferred_dialect == preferred,
                name + ": note port mismatch");
    }

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), name + ": activate failed");
    require(plugin->start_processing(plugin), name + ": start processing failed");

    EventList in;
    AnyEvent event {};
    event.note = { header(1, CLAP_EVENT_NOTE_ON, sizeof(clap_event_note_t), CLAP_EVENT_IS_LIVE),
                   5, 0, 2, 60, 1.0 };
    in.add(event);
    event.midi = { header(2, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t)), 0, { 0xe2, 0x10, 0x50 } };
    in.add(event);
    event.sysex = { header(3, CLAP_EVENT_MIDI_SYSEX, sizeof(clap_event_midi_sysex_t)), 0,
                    sysexBytes, sizeof(sysexBytes) };
    in.add(event);
    event.midi2 = { header(4, CLAP_EVENT_MIDI2, sizeof(clap_event_midi2_t),
                           CLAP_EVENT_DONT_RECORD),
                    0, { 0x40903c00, 0xffff0000, 0, 0 } };
    in.add(event);
    event.note = { header(5, CLAP_EVENT_NOTE_CHOKE, sizeof(clap_event_note_t)), -1, 0, -1, -1,
                   0.0 };
    in.add(event);
    event.value = { header(6, CLAP_EVENT_PARAM_VALUE, sizeof(clap_event_param_value_t)), 1,
                    nullptr, -1, -1, -1, -1, 0.5 };
    in.add(event);
    event.note = { header(7, CLAP_EVENT_NOTE_OFF, sizeof(clap_event_note_t)), 5, 0, 2, 60, 0.0 };
    in.add(event);
    event.note = { header(blockSize, CLAP_EVENT_NOTE_OFF, sizeof(clap_event_note_t)), 5, 0, 2, 60,
                   0.0 };
    in.add(event);

    std::array<float, blockSize> left {}, right {};
    left.fill(-9.0f);
    right.fill(-9.0f);
    std::array<float*, 2> channels { left.data(), right.data() };
    clap_audio_buffer_t output { channels.data(), nullptr, 2, 0, 0 };
    OutputList out;
    clap_process_t process {};
    process.frames_count = blockSize;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &in.events;
    process.out_events = &out.events;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            name + ": status mismatch");

    // Frame: [left, right]; left is the event type plus 1, so CLAP_EVENT_NOTE_ON
    // (0) differs from no event. Every other frame is 0.
    const std::array<std::array<float, 2>, 8> marks { {
        { 0, 0 },
        { CLAP_EVENT_NOTE_ON + 1, CLAP_EVENT_IS_LIVE },
        { CLAP_EVENT_MIDI + 1, 0 },
        { CLAP_EVENT_MIDI_SYSEX + 1, 0 },
        { CLAP_EVENT_MIDI2 + 1, CLAP_EVENT_DONT_RECORD },
        { CLAP_EVENT_NOTE_CHOKE + 1, 0 },
        { CLAP_EVENT_PARAM_VALUE + 1, 0 },
        { CLAP_EVENT_NOTE_OFF + 1, 0 },
    } };
    for (uint32_t frame = 0; frame < blockSize; ++frame)
    {
        const auto expected = frame < marks.size() ? marks[frame] : std::array<float, 2> {};
        require(left[frame] == expected[0] && right[frame] == expected[1],
                name + ": frame " + std::to_string(frame) + " is " + std::to_string(left[frame])
                    + ", " + std::to_string(right[frame]));
    }

    // Echoed in input order, unchanged; the parameter value and the event past
    // the block are not.
    const std::array<uint32_t, 6> echoed { 0, 1, 2, 3, 4, 6 };
    require(out.list.size() == echoed.size(),
            name + ": echoed " + std::to_string(out.list.size()));
    for (uint32_t index = 0; index < echoed.size(); ++index)
    {
        const auto& sent = in.list[echoed[index]];
        require(std::memcmp(&out.list[index], &sent, sent.header.size) == 0,
                name + ": echo " + std::to_string(index) + " differs");
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
}

void runSmoke()
{
    require(clap_entry.init("."), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 3, "factory mismatch");
    require(std::strcmp(factory->get_plugin_descriptor(factory, 0)->id, midiEchoId) == 0
                && std::strcmp(factory->get_plugin_descriptor(factory, 1)->id, midi2EchoId) == 0
                && std::strcmp(factory->get_plugin_descriptor(factory, 2)->id, clapEchoId) == 0
                && !factory->get_plugin_descriptor(factory, 3),
            "descriptor IDs mismatch");
    require(!factory->create_plugin(factory, &host, "unknown"), "created an unknown ID");
    // Each plug-in takes one dialect; the marks and echoes do not depend on it.
    check(factory, midiEchoId, CLAP_NOTE_DIALECT_MIDI, CLAP_NOTE_DIALECT_MIDI);
    check(factory, midi2EchoId, CLAP_NOTE_DIALECT_MIDI2, CLAP_NOTE_DIALECT_MIDI2);
    check(factory, clapEchoId, CLAP_NOTE_DIALECT_CLAP, CLAP_NOTE_DIALECT_CLAP);
    clap_entry.deinit();
}

} // namespace

int main()
{
    try
    {
        runSmoke();
        std::puts("midi-echo native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "midi-echo native smoke failed: %s\n", error.what());
        return 1;
    }
}

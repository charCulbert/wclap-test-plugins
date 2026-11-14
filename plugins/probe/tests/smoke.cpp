#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr uint32_t blockSize = 128;
constexpr char pluginId[] = TEST_PLUGIN_ID;
constexpr clap_id levelParamId = 1;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

union AnyEvent
{
    clap_event_header_t header;
    clap_event_note_t note;
    clap_event_midi_t midi;
    clap_event_param_value_t value;
    clap_event_param_mod_t mod;
    clap_event_note_expression_t expression;
};

clap_event_header_t header(uint32_t time, uint16_t type, uint32_t size)
{
    return { size, time, CLAP_CORE_EVENT_SPACE_ID, type, 0 };
}

enum class Dialect
{
    clap,
    midi
};

struct EventList
{
    Dialect dialect = Dialect::clap;
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

    void note(uint32_t time, uint16_t type, int32_t noteId, int16_t key, int16_t channel = 0,
              int16_t port = 0)
    {
        AnyEvent event {};
        event.note = { header(time, type, sizeof(clap_event_note_t)), noteId, port, channel,
                       key, 1.0 };
        list.push_back(event);
    }

    // A note-on or note-off in the list's dialect. MIDI has no note ID.
    void noteOn(uint32_t time, int32_t noteId, int16_t key, int16_t channel = 0)
    {
        if (dialect == Dialect::midi)
            midi(time, static_cast<uint8_t>(0x90 | channel), static_cast<uint8_t>(key), 100);
        else
            note(time, CLAP_EVENT_NOTE_ON, noteId, key, channel);
    }

    void noteOff(uint32_t time, int16_t key, int16_t channel = 0)
    {
        if (dialect == Dialect::midi)
            midi(time, static_cast<uint8_t>(0x80 | channel), static_cast<uint8_t>(key), 0);
        else
            note(time, CLAP_EVENT_NOTE_OFF, -1, key, channel);
    }

    void midi(uint32_t time, uint8_t status, uint8_t key, uint8_t velocity)
    {
        AnyEvent event {};
        event.midi = { header(time, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t)), 0,
                       { status, key, velocity } };
        list.push_back(event);
    }

    void value(uint32_t time, double value, int32_t noteId = -1)
    {
        AnyEvent event {};
        event.value = { header(time, CLAP_EVENT_PARAM_VALUE, sizeof(clap_event_param_value_t)),
                        levelParamId, nullptr, noteId, -1, -1, -1, value };
        list.push_back(event);
    }

    void mod(uint32_t time, double amount, int32_t noteId = -1, int16_t key = -1)
    {
        AnyEvent event {};
        event.mod = { header(time, CLAP_EVENT_PARAM_MOD, sizeof(clap_event_param_mod_t)),
                      levelParamId, nullptr, noteId, -1, -1, key, amount };
        list.push_back(event);
    }

    void tuning(uint32_t time, double semitones, int32_t noteId, int16_t key = -1)
    {
        AnyEvent event {};
        event.expression = { header(time, CLAP_EVENT_NOTE_EXPRESSION,
                                    sizeof(clap_event_note_expression_t)),
                             CLAP_NOTE_EXPRESSION_TUNING, noteId, -1, -1, key, semitones };
        list.push_back(event);
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

struct Block
{
    std::array<float, blockSize> left {};
    std::array<float, blockSize> right {};
    std::array<float, blockSize> ignored {};
    std::array<float*, 2> outputChannels { left.data(), right.data() };
    std::array<float*, 2> inputChannels { ignored.data(), ignored.data() };
    clap_audio_buffer_t output { outputChannels.data(), nullptr, 2, 0, 0 };
    clap_audio_buffer_t input { inputChannels.data(), nullptr, 2, 0, 0 };
    OutputList out;

    clap_process_status run(const clap_plugin_t* plugin, EventList& events, int64_t steadyTime)
    {
        left.fill(-9.0f);
        right.fill(-9.0f);
        ignored.fill(0.7f);
        out.ends.clear();
        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = blockSize;
        process.audio_inputs = &input;
        process.audio_inputs_count = 1;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1;
        process.in_events = &events.events;
        process.out_events = &out.events;
        return plugin->process(plugin, &process);
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void expect(const std::array<float, blockSize>& samples, uint32_t from, uint32_t to,
            float expected, const char* what)
{
    for (auto frame = from; frame < to; ++frame)
        require(std::abs(samples[frame] - expected) < 1.0e-6f,
                std::string(what) + ": frame " + std::to_string(frame) + " is "
                    + std::to_string(samples[frame]) + ", expected "
                    + std::to_string(expected));
}

struct End
{
    uint32_t time;
    int32_t noteId;
    int16_t channel;
    int16_t key;
};

void expectEnds(const OutputList& out, const std::vector<End>& expected, const std::string& what)
{
    require(out.ends.size() == expected.size(),
            what + ": NOTE_END count is " + std::to_string(out.ends.size()));
    for (size_t index = 0; index < expected.size(); ++index)
    {
        const auto& actual = out.ends[index];
        require(actual.header.time == expected[index].time
                    && actual.note_id == expected[index].noteId && actual.port_index == 0
                    && actual.channel == expected[index].channel
                    && actual.key == expected[index].key,
                what + ": NOTE_END " + std::to_string(index) + " mismatch: time "
                    + std::to_string(actual.header.time) + ", note ID "
                    + std::to_string(actual.note_id) + ", key " + std::to_string(actual.key));
    }
}

// The README's validation protocol, with note-ons and note-offs in one dialect.
// Everything but the NOTE_END note IDs is the same in both.
void runProtocol(const clap_plugin_t* plugin, const clap_plugin_params_t* params, Dialect dialect)
{
    const std::string name = dialect == Dialect::midi ? "MIDI notes" : "CLAP notes";
    const auto id = [dialect](int32_t noteId) { return dialect == Dialect::midi ? -1 : noteId; };

    double value = -1.0;
    {
        EventList reset;
        reset.value(0, 0.0);
        params->flush(plugin, &reset.events, nullptr);
    }
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), name + ": activate failed");
    require(plugin->start_processing(plugin), name + ": start processing failed");

    EventList events;
    events.dialect = dialect;
    events.noteOn(4, 7, 60);
    events.value(8, 0.5);
    events.mod(12, 0.25);
    events.noteOn(16, 8, 62);
    events.mod(20, 0.5, -1, 60);
    events.tuning(24, 12.0, -1, 62);
    events.tuning(28, 32.0, -1);
    events.noteOff(32, 60);
    events.note(36, CLAP_EVENT_NOTE_CHOKE, -1, 62);
    events.noteOn(40, 11, 64, 1);
    events.noteOff(44, 64, 1);
    events.noteOn(46, 12, 67, 2);
    events.noteOff(47, 67, 2);
    // Not sent by a correct host: a note-on with key -1, a value addressed to
    // a voice (not advertised), and an event past the block.
    events.note(48, CLAP_EVENT_NOTE_ON, 9, -1);
    events.value(52, 0.875, 7);
    events.mod(56, 0.0);
    events.note(200, CLAP_EVENT_NOTE_ON, 10, 65);

    Block block;
    require(block.run(plugin, events, 0) == CLAP_PROCESS_CONTINUE, name + ": status mismatch");

    expect(block.left, 0, 4, 0.0f, "left before first note");
    expect(block.left, 4, 5, 1.0f, "note-on click");
    expect(block.left, 5, 16, 0.0f, "left with an unmodulated voice");
    expect(block.left, 16, 17, 1.0f, "second note-on click");
    expect(block.left, 17, 20, 0.0f, "left with unmodulated voices");
    expect(block.left, 20, 24, 0.5f, "per-voice modulation on key 60");
    expect(block.left, 24, 28, 0.59375f, "tuning 12 on key 62");
    expect(block.left, 28, 32, 1.0f, "wildcard tuning 32 on both voices");
    expect(block.left, 32, 33, -0.75f, "note-off click on remaining voice sum");
    expect(block.left, 33, 36, 0.25f, "note-off ended key 60");
    expect(block.left, 36, 37, -1.0f, "choke click");
    expect(block.left, 37, 40, 0.0f, "choke ended key 62");
    expect(block.left, 40, 41, 1.0f, "channel 1 note-on click");
    expect(block.left, 41, 44, 0.0f, "left after channel 1 note-on");
    expect(block.left, 44, 45, -1.0f, "channel 1 note-off click");
    expect(block.left, 45, 46, 0.0f, "left after channel 1 note-off");
    expect(block.left, 46, 47, 1.0f, "channel 2 note-on click");
    expect(block.left, 47, 48, -1.0f, "channel 2 note-off click");
    expect(block.left, 48, 127, 0.0f, "invalid note-on ignored");
    expect(block.left, 127, 128, 1.0f, "late note-on on last frame");

    expect(block.right, 0, 8, 0.0f, "default level");
    expect(block.right, 8, 12, 0.5f, "level 0.5");
    expect(block.right, 12, 56, 0.75f, "level plus global modulation");
    expect(block.right, 56, 128, 0.5f, "global modulation cleared");

    expectEnds(block.out,
               { { 32, id(7), 0, 60 }, { 36, id(8), 0, 62 }, { 44, id(11), 1, 64 },
                 { 47, id(12), 2, 67 } },
               name);

    require(params->get_value(plugin, levelParamId, &value) && value == 0.5,
            name + ": get_value does not report the processed level");

    plugin->stop_processing(plugin);
    EventList flush;
    flush.value(0, 0.25);
    flush.mod(0, 0.125);
    params->flush(plugin, &flush.events, nullptr);
    require(params->get_value(plugin, levelParamId, &value) && value == 0.25,
            name + ": flush did not apply the level");

    require(plugin->start_processing(plugin), name + ": restart processing failed");
    EventList none;
    require(block.run(plugin, none, blockSize) == CLAP_PROCESS_CONTINUE,
            name + ": status mismatch");
    expect(block.right, 0, blockSize, 0.375f, "flushed level and modulation");
    expect(block.left, 0, blockSize, 0.0f, "left with one unmodulated voice");
    require(block.out.ends.empty(), name + ": NOTE_END sent with no note-off");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
}

// NOTE_END order follows note-on order even when a voice slot is reused, and a
// note-on with no free voice ends at once.
void runVoiceChecks(const clap_plugin_t* plugin)
{
    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");

    Block block;
    EventList reuse;
    reuse.note(0, CLAP_EVENT_NOTE_ON, 1, 60);
    reuse.note(1, CLAP_EVENT_NOTE_ON, 2, 61);
    reuse.note(2, CLAP_EVENT_NOTE_OFF, 1, 60);
    reuse.note(3, CLAP_EVENT_NOTE_ON, 3, 62); // takes the first voice slot back
    reuse.note(4, CLAP_EVENT_NOTE_OFF, -1, -1);
    require(block.run(plugin, reuse, 0) == CLAP_PROCESS_CONTINUE, "status mismatch");
    expectEnds(block.out, { { 2, 1, 0, 60 }, { 4, 2, 0, 61 }, { 4, 3, 0, 62 } },
               "slot reuse");

    EventList full;
    for (int16_t key = 0; key < 65; ++key) full.note(key, CLAP_EVENT_NOTE_ON, key, key);
    full.note(100, CLAP_EVENT_NOTE_OFF, -1, -1);
    require(block.run(plugin, full, blockSize) == CLAP_PROCESS_CONTINUE, "status mismatch");
    std::vector<End> ends { { 64, 64, 0, 64 } };
    for (int16_t key = 0; key < 64; ++key) ends.push_back({ 100, key, 0, key });
    expectEnds(block.out, ends, "no free voice");
    expect(block.left, 64, 65, 1.0f, "note-on with no free voice still clicks");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "probe-smoke", "wclap-test-plugins", "", "0.1.0",
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
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    require(audioPorts && audioPorts->count(plugin, true) == 1
                && audioPorts->count(plugin, false) == 1,
            "audio port count mismatch");
    clap_audio_port_info_t audioInfo {};
    require(audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 2,
            "output is not stereo");
    require(audioPorts->get(plugin, 0, true, &audioInfo) && audioInfo.channel_count == 2,
            "input is not stereo");
    clap_note_port_info_t noteInfo {};
    require(notePorts && notePorts->count(plugin, true) == 1
                && notePorts->count(plugin, false) == 0
                && notePorts->get(plugin, 0, true, &noteInfo)
                && noteInfo.supported_dialects
                    == (CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI),
            "note port mismatch");
    clap_param_info_t paramInfo {};
    require(params && params->count(plugin) == 1 && params->get_info(plugin, 0, &paramInfo)
                && paramInfo.id == levelParamId && paramInfo.min_value == 0.0
                && paramInfo.max_value == 1.0 && paramInfo.default_value == 0.0,
            "parameter info mismatch");
    const auto modFlags = CLAP_PARAM_IS_MODULATABLE | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID
        | CLAP_PARAM_IS_MODULATABLE_PER_KEY | CLAP_PARAM_REQUIRES_PROCESS;
    require((paramInfo.flags & modFlags) == modFlags, "parameter flags mismatch");

    // The note port takes both dialects, so the host may send either.
    runProtocol(plugin, params, Dialect::clap);
    runProtocol(plugin, params, Dialect::midi);
    runVoiceChecks(plugin);

    double value = -1.0;
    char text[32] {};
    require(params->value_to_text(plugin, levelParamId, 0.5, text, sizeof(text))
                && params->text_to_value(plugin, levelParamId, text, &value) && value == 0.5,
            "text round trip failed");

    plugin->destroy(plugin);
    clap_entry.deinit();
}

} // namespace

int main()
{
    try
    {
        runSmoke();
        std::puts("probe native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "probe native smoke failed: %s\n", error.what());
        return 1;
    }
}

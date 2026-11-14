#include "Plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/state-context.h>
#include <clap/ext/state.h>
#include <clap/factory/plugin-factory.h>

#include <algorithm>
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

constexpr uint32_t blockSize = 16;
constexpr char pluginId[] = TEST_PLUGIN_ID;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

using Bytes = std::vector<uint8_t>;

struct Writer
{
    Bytes bytes;
    clap_ostream_t stream { this, write };

    static int64_t write(const clap_ostream_t* stream, const void* buffer, uint64_t size)
    {
        // At most 5 bytes per call, so the plug-in must loop.
        size = std::min<uint64_t>(size, 5);
        auto& bytes = static_cast<Writer*>(stream->ctx)->bytes;
        const auto* data = static_cast<const uint8_t*>(buffer);
        bytes.insert(bytes.end(), data, data + size);
        return static_cast<int64_t>(size);
    }
};

struct Reader
{
    Bytes bytes;
    size_t position = 0;
    clap_istream_t stream { this, read };

    explicit Reader(Bytes bytes) : bytes(std::move(bytes)) {}

    static int64_t read(const clap_istream_t* stream, void* buffer, uint64_t size)
    {
        auto& self = *static_cast<Reader*>(stream->ctx);
        size = std::min<uint64_t>({ size, 5, self.bytes.size() - self.position });
        std::memcpy(buffer, self.bytes.data() + self.position, size);
        self.position += size;
        return static_cast<int64_t>(size);
    }
};

// Processes one block and checks both channels hold the expected values throughout.
void expectOutput(const clap_plugin_t* plugin, float savedFor, float loadedWith, const char* what)
{
    std::array<float, blockSize> left {}, right {};
    left.fill(-9.0f);
    right.fill(-9.0f);
    std::array<float*, 2> channels { left.data(), right.data() };
    clap_audio_buffer_t output { channels.data(), nullptr, 2, 0, 0 };
    clap_process_t process {};
    process.frames_count = blockSize;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            std::string(what) + ": status mismatch");
    for (uint32_t frame = 0; frame < blockSize; ++frame)
        require(left[frame] == savedFor && right[frame] == loadedWith,
                std::string(what) + ": frame " + std::to_string(frame) + " is "
                    + std::to_string(left[frame]) + ", " + std::to_string(right[frame]));
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "state-context-smoke", "wclap-test-plugins", "", "0.1.0",
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
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto* context = static_cast<const clap_plugin_state_context_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE_CONTEXT));
    clap_audio_port_info_t audioInfo {};
    require(audioPorts && audioPorts->count(plugin, true) == 0
                && audioPorts->count(plugin, false) == 1
                && audioPorts->get(plugin, 0, false, &audioInfo) && audioInfo.channel_count == 2,
            "audio port mismatch");
    require(state && context, "state or state-context missing");

    require(plugin->activate(plugin, 48'000.0, blockSize, blockSize), "activate failed");
    require(plugin->start_processing(plugin), "start processing failed");
    expectOutput(plugin, -1.0f, -1.0f, "nothing loaded");

    // Save once per call; index 0 is plain state.save, 1 to 3 the contexts.
    std::array<Bytes, 4> saved;
    {
        Writer writer;
        require(state->save(plugin, &writer.stream), "state.save failed");
        saved[0] = writer.bytes;
    }
    for (uint32_t kind = CLAP_STATE_CONTEXT_FOR_PRESET; kind <= CLAP_STATE_CONTEXT_FOR_PROJECT;
         ++kind)
    {
        Writer writer;
        require(context->save(plugin, &writer.stream, kind),
                "state_context.save failed for context " + std::to_string(kind));
        saved[kind] = writer.bytes;
    }
    // A context this plug-in does not know is saved as plain state.
    Writer future;
    require(context->save(plugin, &future.stream, 9) && future.bytes == saved[0],
            "a save for an unknown context did not write plain state");

    // Loading never changes what a save writes, so every pair is checked.
    for (uint32_t from = 0; from < saved.size(); ++from)
    {
        for (uint32_t with = 0; with <= CLAP_STATE_CONTEXT_FOR_PROJECT; ++with)
        {
            Reader reader(saved[from]);
            const auto loaded = with == 0 ? state->load(plugin, &reader.stream)
                                          : context->load(plugin, &reader.stream, with);
            const auto pair = "saved for " + std::to_string(from) + ", loaded with "
                + std::to_string(with);
            require(loaded, pair + ": load failed");
            expectOutput(plugin, static_cast<float>(from), static_cast<float>(with),
                         pair.c_str());
        }
    }

    Bytes garbage(saved[1].size(), 0x5a);
    Reader junk(garbage);
    require(!state->load(plugin, &junk.stream), "loaded junk");
    Bytes longer = saved[1];
    longer.push_back(0);
    Reader tooLong(longer);
    require(!state->load(plugin, &tooLong.stream), "loaded a state with trailing bytes");
    Bytes shorter(saved[1].begin(), saved[1].end() - 1);
    Reader tooShort(shorter);
    require(!state->load(plugin, &tooShort.stream), "loaded a short state");
    Bytes newer = saved[1];
    newer[4] = 2; // version 2
    Reader wrongVersion(newer);
    require(!state->load(plugin, &wrongVersion.stream), "loaded a state of another version");
    Bytes strange = saved[1];
    strange[8] = 7; // saved for context 7
    Reader unknownSavedFor(strange);
    require(!state->load(plugin, &unknownSavedFor.stream),
            "loaded a state with an unknown saved-for value");
    expectOutput(plugin, 3.0f, 3.0f, "rejected loads change nothing");

    // A context this plug-in does not know is loaded as plain state.
    Reader unknown(saved[1]);
    require(context->load(plugin, &unknown.stream, 9), "load with an unknown context failed");
    expectOutput(plugin, 1.0f, 0.0f, "unknown context loads as plain state");

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
        std::puts("state-context native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "state-context native smoke failed: %s\n", error.what());
        return 1;
    }
}

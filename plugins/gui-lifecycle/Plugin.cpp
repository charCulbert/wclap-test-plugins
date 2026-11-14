#include "Plugin.h"

#include <clap/ext/draft/webview.h>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace wclap_test_plugins::gui_lifecycle
{
namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;

// entry.h: init and deinit may be called more than once, in matched pairs, so
// count them and clear the root only on the last deinit.
std::mutex entryMutex;
unsigned entryCount = 0;
std::string resourceRoot;

const char* mediaTypeFor(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    const auto extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    if (extension == "css") return "text/css";
    if (extension == "js") return "text/javascript";
    if (extension == "txt") return "text/plain";
    if (extension == "html" || extension == "htm") return "text/html";
    return "application/octet-stream";
}

// stream.h: a host may take fewer bytes per write than offered, so write until
// all are taken. Fails on an error (negative) or a write that takes nothing.
bool writeAll(const clap_ostream_t* stream, const uint8_t* bytes, size_t size)
{
    while (size > 0)
    {
        const auto written = stream->write(stream, bytes, size);
        if (written <= 0) return false;
        bytes += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

class GuiPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit GuiPlugin(const clap_host_t* host) : Base(&descriptor(), host), host(host) {}

protected:
    bool init() noexcept override
    {
        hostWebview = static_cast<const clap_host_webview_t*>(
            host->get_extension(host, CLAP_EXT_WEBVIEW));
        return true;
    }

    bool activate(double, uint32_t, uint32_t) noexcept override { return true; }
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (!process || process->audio_inputs_count == 0 || process->audio_outputs_count == 0)
            return CLAP_PROCESS_ERROR;
        const auto& input = process->audio_inputs[0];
        auto& output = process->audio_outputs[0];
        const auto channels = std::min(input.channel_count, output.channel_count);
        for (uint32_t channel = 0; channel < output.channel_count; ++channel)
        {
            auto* destination = output.data32 ? output.data32[channel] : nullptr;
            const auto* source = channel < channels && input.data32
                ? input.data32[channel] : nullptr;
            if (!destination) continue;
            for (uint32_t frame = 0; frame < process->frames_count; ++frame)
                destination[frame] = source ? source[frame] : 0.0f;
        }
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool) const noexcept override { return 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info_t* info) const noexcept override
    {
        if (index != 0 || !info) return false;
        *info = {};
        info->id = isInput ? 0x47554949 : 0x4755494f;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = isInput ? 0x4755494f : 0x47554949;
        std::snprintf(info->name, sizeof(info->name), "%s",
                      isInput ? "Stereo Input" : "Stereo Output");
        return true;
    }

    // clap.webview/3 is a draft extension. The plug-in serves it itself instead
    // of enabling the helpers' draft extensions, which would also expose
    // background_activation and background_state_context, which this plug-in
    // does not implement.
    const void* extension(const char* id) noexcept override
    {
        if (id && std::strcmp(id, CLAP_EXT_WEBVIEW) == 0) return &webviewExtension;
        return nullptr;
    }

    int32_t pageUri(char* uri, uint32_t capacity) noexcept
    {
        constexpr char path[] = "/ui/index.html";
        if (uri && capacity) std::snprintf(uri, capacity, "%s", path);
        uriRead = true;
        return sizeof(path);
    }

    bool pageResource(const char* path, char* mime, uint32_t mimeCapacity,
                            const clap_ostream_t* stream) noexcept
    {
        if (!path || !stream || std::strstr(path, "..")) return false;
        std::string relative = path;
        while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
        if (relative.empty()) return false;

        // entry.h: the entry path is the bundle on macOS, where resources sit in
        // Contents/Resources, and the DSO file on Linux and Windows, where they
        // sit in its directory. Under WASI it is the plug-in directory.
        std::ifstream input;
        const auto slash = resourceRoot.find_last_of("/\\");
        const std::array<std::string, 3> roots {
            resourceRoot, resourceRoot + "/Contents/Resources",
            slash == std::string::npos ? std::string(".") : resourceRoot.substr(0, slash)
        };
        for (const auto& root : roots)
        {
            auto filePath = root + "/" + relative;
            input.open(filePath, std::ios::binary);
            if (input.is_open()) break;
            input.clear();
            // Under WASI, open() only resolves paths inside a preopened directory.
            // A host may preopen the plug-in directory as "." rather than at its
            // absolute path, so retry the same path relative to the current one.
            if (!filePath.empty() && filePath.front() == '/')
            {
                input.open(filePath.substr(1), std::ios::binary);
                if (input.is_open()) break;
                input.clear();
            }
        }
        if (!input.is_open()) return false;
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
        // webview.h: the type is written only if the buffer is large enough, and
        // the call returns true only if the type and all the data were provided.
        const char* type = mediaTypeFor(relative);
        const bool typeFits = mime && mimeCapacity > std::strlen(type);
        if (typeFits) std::memcpy(mime, type, std::strlen(type) + 1);
        const bool written = writeAll(stream, bytes.data(), bytes.size());
        return typeFits && written;
    }

    bool pageMessage(const void* buffer, uint32_t size) noexcept
    {
        // webview.h: get_uri is called at least once before the host accepts
        // messages. The page may send as soon as it loads, before set_parent
        // or show, so messages are taken from then until gui.destroy.
        if (!created || !uriRead)
        {
            hostMisbehaving("webview.receive before gui.create and webview.get_uri; refused");
            return false;
        }
        if (!buffer || size == 0) return false;
        const auto command = static_cast<Command>(*static_cast<const uint8_t*>(buffer));
        const std::array<uint8_t, 2> reply {
            static_cast<uint8_t>(command),
            static_cast<uint8_t>(command == Command::ready)
        };
        return hostWebview && hostWebview->send(host, reply.data(), reply.size());
    }

    bool implementsGui() const noexcept override { return true; }
    bool guiIsApiSupported(const char* api, bool floating) noexcept override
    {
        return !floating && hostWebview && api
            && std::strcmp(api, CLAP_WINDOW_API_WEBVIEW) == 0;
    }
    bool guiGetPreferredApi(const char** api, bool* floating) noexcept override
    {
        // As is_api_supported: no preferred API without the host's clap.webview.
        if (!api || !floating || !hostWebview) return false;
        *api = CLAP_WINDOW_API_WEBVIEW;
        *floating = false;
        return true;
    }
    bool guiCreate(const char* api, bool floating) noexcept override
    {
        if (created || !guiIsApiSupported(api, floating)) return false;
        created = true;
        return true;
    }
    void guiDestroy() noexcept override { created = visible = parented = false; }
    bool guiShow() noexcept override
    {
        // gui.h lists set_parent before show for an embedded window.
        if (!created || !parented)
        {
            hostMisbehaving("gui.show before a successful gui.set_parent; refused");
            return false;
        }
        visible = true;
        return true;
    }
    bool guiHide() noexcept override
    {
        if (!created) return false;
        visible = false;
        return true;
    }
    bool guiGetSize(uint32_t* outputWidth, uint32_t* outputHeight) noexcept override
    {
        if (!created || !outputWidth || !outputHeight) return false;
        *outputWidth = width;
        *outputHeight = height;
        return true;
    }
    bool guiSetParent(const clap_window_t* window) noexcept override
    {
        // webview.h: the window pointer must be null.
        parented = created && window && window->api
            && std::strcmp(window->api, CLAP_WINDOW_API_WEBVIEW) == 0
            && window->ptr == nullptr;
        if (!parented)
            hostMisbehaving("gui.set_parent without a webview window with a null ptr; refused");
        return parented;
    }

private:
    static GuiPlugin& self(const clap_plugin_t* plugin) noexcept
    {
        return static_cast<GuiPlugin&>(*static_cast<Base*>(plugin->plugin_data));
    }
    static int32_t clapGetUri(const clap_plugin_t* plugin, char* uri, uint32_t capacity) noexcept
    {
        return self(plugin).pageUri(uri, capacity);
    }
    static bool clapGetResource(const clap_plugin_t* plugin, const char* path, char* mime,
                                uint32_t mimeCapacity, const clap_ostream_t* stream) noexcept
    {
        return self(plugin).pageResource(path, mime, mimeCapacity, stream);
    }
    static bool clapReceive(const clap_plugin_t* plugin, const void* buffer,
                            uint32_t size) noexcept
    {
        return self(plugin).pageMessage(buffer, size);
    }
    static constexpr clap_plugin_webview_t webviewExtension {
        clapGetUri, clapGetResource, clapReceive
    };

    const clap_host_t* host;
    const clap_host_webview_t* hostWebview = nullptr;
    static constexpr uint32_t width = 400;
    static constexpr uint32_t height = 260;
    bool created = false;
    bool uriRead = false;
    bool parented = false;
    bool visible = false;
};

uint32_t pluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* pluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &descriptor() : nullptr;
}
const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
                                  const clap_host_t* host, const char* id)
{
    if (!host || !id || std::strcmp(id, pluginId) != 0) return nullptr;
    return (new GuiPlugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] {
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr
    };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "WCLAP GUI Lifecycle Test", "Charlie Culbert",
        TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_URL, TEST_PLUGIN_VERSION,
        "Strict embedded WebView GUI lifecycle fixture", features
    };
    return value;
}

bool entryInit(const char* path)
{
    if (!path) return false;
    std::lock_guard<std::mutex> lock(entryMutex);
    if (entryCount++ == 0) resourceRoot = path;
    return true;
}
void entryDeinit()
{
    std::lock_guard<std::mutex> lock(entryMutex);
    if (entryCount > 0 && --entryCount == 0) resourceRoot.clear();
}
const void* entryGetFactory(const char* factoryId)
{
    if (!factoryId || std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) != 0) return nullptr;
    static const clap_plugin_factory_t factory { pluginCount, pluginDescriptor, createPlugin };
    return &factory;
}

} // namespace wclap_test_plugins::gui_lifecycle

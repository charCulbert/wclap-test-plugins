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
#include <string>
#include <vector>

namespace wclap_test_plugins::gui_lifecycle
{
namespace
{

constexpr char pluginId[] = "com.charlieculbert.wclap-test-plugins.gui-lifecycle";
std::string resourceRoot;

const char* mediaTypeFor(const std::string& path)
{
    const auto extension = path.substr(path.find_last_of('.') + 1);
    if (extension == "css") return "text/css";
    if (extension == "js") return "text/javascript";
    if (extension == "txt") return "text/plain";
    return "text/html";
}

class GuiPlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
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

    bool enableDraftExtensions() const noexcept override { return true; }
    bool implementsWebview() const noexcept override { return true; }

    int32_t webviewGetUri(char* uri, uint32_t capacity) const noexcept override
    {
        constexpr char path[] = "/ui/index.html";
        if (uri && capacity) std::snprintf(uri, capacity, "%s", path);
        return sizeof(path);
    }

    bool webviewGetResource(const char* path, char* mime, uint32_t mimeCapacity,
                            const clap_ostream_t* stream) override
    {
        if (!path || !stream || std::strstr(path, "..")) return false;
        std::string relative = path;
        while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
        if (relative.empty()) return false;

        std::ifstream input;
        const std::array<std::string, 2> roots {
            resourceRoot, resourceRoot + "/Contents/Resources"
        };
        for (const auto& root : roots)
        {
            auto filePath = root + "/" + relative;
            input.open(filePath, std::ios::binary);
            if (input) break;
            input.clear();
            if (!filePath.empty() && filePath.front() == '/')
                input.open(filePath.substr(1), std::ios::binary);
            if (input) break;
            input.clear();
        }
        if (!input) return false;
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
        if (mime && mimeCapacity)
            std::snprintf(mime, mimeCapacity, "%s", mediaTypeFor(relative));
        return bytes.empty() || stream->write(stream, bytes.data(), bytes.size())
            == static_cast<int64_t>(bytes.size());
    }

    bool webviewReceive(const void* buffer, uint32_t size) const noexcept override
    {
        if (!created || !visible || !buffer || size == 0) return false;
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
        if (!api || !floating) return false;
        *api = CLAP_WINDOW_API_WEBVIEW;
        *floating = false;
        return true;
    }
    bool guiCreate(const char* api, bool floating) noexcept override
    {
        if (created || !guiIsApiSupported(api, floating)) return false;
        created = true;
        sized = false;
        return true;
    }
    void guiDestroy() noexcept override { created = sized = visible = parented = false; }
    bool guiShow() noexcept override { return created && parented && (visible = true); }
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
        sized = true;
        return true;
    }
    bool guiSetParent(const clap_window_t* window) noexcept override
    {
        parented = created && sized && window && window->api
            && std::strcmp(window->api, CLAP_WINDOW_API_WEBVIEW) == 0
            && window->ptr == nullptr;
        return parented;
    }

private:
    const clap_host_t* host;
    const clap_host_webview_t* hostWebview = nullptr;
    static constexpr uint32_t width = 400;
    static constexpr uint32_t height = 260;
    mutable bool created = false;
    mutable bool sized = false;
    mutable bool parented = false;
    mutable bool visible = false;
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
        "", "", "", "0.1.0", "Strict embedded WebView GUI lifecycle fixture", features
    };
    return value;
}

bool entryInit(const char* path)
{
    if (!path) return false;
    resourceRoot = path;
    return true;
}
void entryDeinit() { resourceRoot.clear(); }
const void* entryGetFactory(const char* factoryId)
{
    if (!factoryId || std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) != 0) return nullptr;
    static const clap_plugin_factory_t factory { pluginCount, pluginDescriptor, createPlugin };
    return &factory;
}

} // namespace wclap_test_plugins::gui_lifecycle

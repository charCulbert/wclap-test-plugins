#include "Plugin.h"

#include <clap/ext/draft/webview.h>
#include <clap/ext/gui.h>
#include <clap/factory/plugin-factory.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace
{

constexpr char pluginId[] = TEST_PLUGIN_ID;

std::vector<std::vector<uint8_t>> sent;

bool hostSend(const clap_host_t*, const void* buffer, uint32_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    sent.emplace_back(bytes, bytes + size);
    return true;
}
const clap_host_webview_t hostWebview { hostSend };

const void* hostGetExtension(const clap_host_t*, const char* id)
{
    return std::strcmp(id, CLAP_EXT_WEBVIEW) == 0 ? &hostWebview : nullptr;
}
void hostRequest(const clap_host_t*) {}

// A stream that takes at most three bytes per write, as stream.h allows.
struct SlowStream
{
    std::string data;
    clap_ostream_t stream { this, write };

    static int64_t write(const clap_ostream_t* stream, const void* buffer, uint64_t size)
    {
        const auto taken = size < 3 ? size : 3;
        static_cast<SlowStream*>(stream->ctx)->data.append(static_cast<const char*>(buffer),
                                                           taken);
        return static_cast<int64_t>(taken);
    }
};

// A stream that takes nothing.
int64_t stuckWrite(const clap_ostream_t*, const void*, uint64_t) { return 0; }

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

// The resources directory beside this test's source, standing in for a bundle.
std::string resourcesDirectory()
{
    std::string path = __FILE__;
    const auto slash = path.find_last_of("/\\");
    require(slash != std::string::npos, "no directory in __FILE__");
    return path.substr(0, slash) + "/../resources";
}

void runSmoke()
{
    const clap_host_t host {
        CLAP_VERSION, nullptr, "gui-lifecycle-smoke", "wclap-test-plugins", "", "0.1.0",
        hostGetExtension, hostRequest, hostRequest, hostRequest
    };

    const auto resources = resourcesDirectory();
    // As on Linux and Windows, the entry path names a file; resources are
    // looked up in its directory.
    require(clap_entry.init((resources + "/gui-lifecycle.clap").c_str()), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "factory mismatch");
    const auto* plugin = factory->create_plugin(factory, &host, pluginId);
    require(plugin && plugin->init(plugin), "plugin init failed");

    const auto* gui = static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto* webview = static_cast<const clap_plugin_webview_t*>(
        plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    require(gui && webview, "gui or webview extension missing");

    char uri[64] {};
    require(webview->get_uri(plugin, uri, sizeof(uri)) > 0
                && std::strcmp(uri, "/ui/index.html") == 0,
            "uri mismatch");

    SlowStream page;
    char mime[64] {};
    require(webview->get_resource(plugin, "/ui/message.txt", mime, sizeof(mime), &page.stream),
            "get_resource failed with a stream that takes three bytes at a time");
    require(page.data == "Multi-file resources ready.\n", "resource bytes mismatch: " + page.data);
    require(std::strcmp(mime, "text/plain") == 0, "message.txt media type mismatch");
    SlowStream html;
    require(webview->get_resource(plugin, "/ui/index.html", mime, sizeof(mime), &html.stream)
                && std::strcmp(mime, "text/html") == 0,
            "index.html mismatch");

    const clap_ostream_t stuck { nullptr, stuckWrite };
    require(!webview->get_resource(plugin, "/ui/message.txt", mime, sizeof(mime), &stuck),
            "get_resource succeeded with a stream that takes nothing");
    SlowStream missing;
    require(!webview->get_resource(plugin, "/ui/missing.txt", mime, sizeof(mime), &missing.stream),
            "get_resource found a missing file");
    require(!webview->get_resource(plugin, "/../README.md", mime, sizeof(mime), &missing.stream),
            "get_resource left the resource directory");

    const uint8_t ready = static_cast<uint8_t>(wclap_test_plugins::gui_lifecycle::Command::ready);
    require(!webview->receive(plugin, &ready, 1), "message accepted before create");

    require(gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, false)
                && !gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, true),
            "api support mismatch");
    require(gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false), "create failed");
    clap_window_t window {};
    window.api = CLAP_WINDOW_API_WEBVIEW;
    require(!gui->set_parent(plugin, &window), "set_parent succeeded before get_size");
    uint32_t width = 0, height = 0;
    require(gui->get_size(plugin, &width, &height) && width == 400 && height == 260,
            "size mismatch");
    require(!gui->show(plugin), "show succeeded before set_parent");
    require(!webview->receive(plugin, &ready, 1), "message accepted before set_parent");
    require(gui->set_parent(plugin, &window), "set_parent failed");

    // The page may send its ready message before show.
    require(webview->receive(plugin, &ready, 1), "message rejected after set_parent");
    require(sent.size() == 1 && sent[0] == std::vector<uint8_t> { ready, 1 }, "reply mismatch");
    require(gui->show(plugin), "show failed");
    require(gui->hide(plugin), "hide failed");
    require(webview->receive(plugin, &ready, 1), "message rejected while hidden");
    gui->destroy(plugin);
    require(!webview->receive(plugin, &ready, 1), "message accepted after destroy");

    plugin->destroy(plugin);
    clap_entry.deinit();
}

} // namespace

int main()
{
    try
    {
        runSmoke();
        std::puts("gui-lifecycle native smoke passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "gui-lifecycle native smoke failed: %s\n", error.what());
        return 1;
    }
}

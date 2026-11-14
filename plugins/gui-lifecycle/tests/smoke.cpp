#include "Plugin.h"

#include <clap/ext/draft/background-activation.h>
#include <clap/ext/draft/background-state-context.h>
#include <clap/ext/draft/webview.h>
#include <clap/ext/gui.h>
#include <clap/ext/log.h>
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

uint32_t misbehaviourLogs = 0;
void hostLog(const clap_host_t*, clap_log_severity severity, const char*)
{
    if (severity == CLAP_LOG_HOST_MISBEHAVING) ++misbehaviourLogs;
}
const clap_host_log_t hostLogExtension { hostLog };

const void* hostGetExtension(const clap_host_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_WEBVIEW) == 0) return &hostWebview;
    if (std::strcmp(id, CLAP_EXT_LOG) == 0) return &hostLogExtension;
    return nullptr;
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

    const uint8_t ready = static_cast<uint8_t>(wclap_test_plugins::gui_lifecycle::Command::ready);
    // webview.h: no messages before get_uri, even with the GUI created.
    require(gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false), "create failed");
    auto logs = misbehaviourLogs;
    require(!webview->receive(plugin, &ready, 1), "message accepted before get_uri");
    require(misbehaviourLogs == logs + 1, "refused message was not logged");
    gui->destroy(plugin);

    char uri[64] {};
    require(webview->get_uri(plugin, uri, sizeof(uri)) > 0
                && std::strcmp(uri, "/ui/index.html") == 0,
            "uri mismatch");
    require(webview->get_uri(plugin, nullptr, 0) == static_cast<int32_t>(sizeof("/ui/index.html")),
            "uri length mismatch");

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

    // webview.h: the type is written only if it fits; true needs type and data.
    char shortMime[5] = "xxxx";
    SlowStream shortType;
    require(!webview->get_resource(plugin, "/ui/message.txt", shortMime, sizeof(shortMime),
                                   &shortType.stream),
            "get_resource returned true without the media type");
    require(std::strcmp(shortMime, "xxxx") == 0, "a truncated media type was written");

    const clap_ostream_t stuck { nullptr, stuckWrite };
    require(!webview->get_resource(plugin, "/ui/message.txt", mime, sizeof(mime), &stuck),
            "get_resource succeeded with a stream that takes nothing");
    SlowStream missing;
    require(!webview->get_resource(plugin, "/ui/missing.txt", mime, sizeof(mime), &missing.stream),
            "get_resource found a missing file");
    require(!webview->get_resource(plugin, "/../README.md", mime, sizeof(mime), &missing.stream),
            "get_resource left the resource directory");

    logs = misbehaviourLogs;
    require(!webview->receive(plugin, &ready, 1), "message accepted before create");
    require(misbehaviourLogs == logs + 1, "refused message was not logged");

    // Out of order: refused and logged, the process keeps running.
    logs = misbehaviourLogs;
    uint32_t width = 0, height = 0;
    require(!gui->show(plugin), "show succeeded before create");
    require(!gui->get_size(plugin, &width, &height), "get_size succeeded before create");
    require(!gui->set_size(plugin, 100, 100), "set_size succeeded before create");
    gui->destroy(plugin);
    require(misbehaviourLogs == logs + 4, "out-of-order calls were not all logged");

    require(gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, false)
                && !gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, true),
            "api support mismatch");
    require(gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false), "create failed");
    require(!gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false), "second create succeeded");
    // The page may send before set_parent and show.
    require(webview->receive(plugin, &ready, 1), "message rejected after create and get_uri");
    require(sent.size() == 1 && sent[0] == std::vector<uint8_t> { ready, 1 }, "reply mismatch");
    const uint8_t unknown = 9;
    require(webview->receive(plugin, &unknown, 1)
                && sent.size() == 2 && sent[1] == std::vector<uint8_t> { unknown, 0 },
            "unknown command reply mismatch");

    logs = misbehaviourLogs;
    require(!gui->show(plugin), "show succeeded before set_parent");
    require(!gui->set_size(plugin, 100, 100), "set_size succeeded on a fixed-size GUI");
    require(misbehaviourLogs == logs + 2, "refused show or set_size was not logged");

    clap_window_t window {};
    window.api = CLAP_WINDOW_API_WEBVIEW;
    // gui.h describes get_size before set_parent; the plug-in does not require it.
    require(gui->set_parent(plugin, &window), "set_parent failed");
    require(gui->get_size(plugin, &width, &height) && width == 400 && height == 260,
            "size mismatch");
    require(gui->show(plugin), "show failed");
    require(gui->hide(plugin), "hide failed");
    require(webview->receive(plugin, &ready, 1), "message rejected while hidden");
    require(gui->show(plugin), "show after hide failed");
    gui->destroy(plugin);
    require(!webview->receive(plugin, &ready, 1), "message accepted after destroy");

    // destroy with the GUI still created: logged, GUI destroyed, no crash.
    require(gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false), "create after destroy failed");
    logs = misbehaviourLogs;
    plugin->destroy(plugin);
    require(misbehaviourLogs == logs + 1, "destroy with the GUI open was not logged");

    // entry.h: init and deinit may be called more than once in matched pairs;
    // the resource root stays until the last deinit.
    require(clap_entry.init((resources + "/gui-lifecycle.clap").c_str()), "second init failed");
    clap_entry.deinit();
    const auto* again = factory->create_plugin(factory, &host, pluginId);
    require(again && again->init(again), "plugin init after nested deinit failed");
    const auto* webviewAgain = static_cast<const clap_plugin_webview_t*>(
        again->get_extension(again, CLAP_EXT_WEBVIEW));
    SlowStream after;
    require(webviewAgain->get_resource(again, "/ui/message.txt", mime, sizeof(mime),
                                       &after.stream),
            "resources lost after a nested deinit");
    require(!again->get_extension(again, CLAP_EXT_BACKGROUND_ACTIVATION)
                && !again->get_extension(again, CLAP_EXT_BACKGROUND_STATE_CONTEXT),
            "unimplemented background extensions exposed");
    again->destroy(again);
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

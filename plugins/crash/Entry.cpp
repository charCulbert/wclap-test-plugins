#include "Plugin.h"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    wclap_test_plugins::crash::entryInit,
    wclap_test_plugins::crash::entryDeinit,
    wclap_test_plugins::crash::entryGetFactory
};

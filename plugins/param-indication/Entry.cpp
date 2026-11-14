#include "Plugin.h"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    wclap_test_plugins::param_indication::entryInit,
    wclap_test_plugins::param_indication::entryDeinit,
    wclap_test_plugins::param_indication::entryGetFactory
};

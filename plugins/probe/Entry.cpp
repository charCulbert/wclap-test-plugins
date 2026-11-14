#include "Plugin.h"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    wclap_test_plugins::probe::entryInit,
    wclap_test_plugins::probe::entryDeinit,
    wclap_test_plugins::probe::entryGetFactory
};

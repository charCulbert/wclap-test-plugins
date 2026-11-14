#include "Plugin.h"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    wclap_test_plugins::note_name::entryInit,
    wclap_test_plugins::note_name::entryDeinit,
    wclap_test_plugins::note_name::entryGetFactory
};

#include "Plugin.h"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    wclap_test_plugins::voice_info::entryInit,
    wclap_test_plugins::voice_info::entryDeinit,
    wclap_test_plugins::voice_info::entryGetFactory
};

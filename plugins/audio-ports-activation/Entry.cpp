#include "Plugin.h"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    wclap_test_plugins::audio_ports_activation::entryInit,
    wclap_test_plugins::audio_ports_activation::entryDeinit,
    wclap_test_plugins::audio_ports_activation::entryGetFactory
};

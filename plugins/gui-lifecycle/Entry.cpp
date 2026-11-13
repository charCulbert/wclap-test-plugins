#include "Plugin.h"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    example::runtime_example::gui::entryInit,
    example::runtime_example::gui::entryDeinit,
    example::runtime_example::gui::entryGetFactory
};

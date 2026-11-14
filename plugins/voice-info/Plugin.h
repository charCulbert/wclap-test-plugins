#pragma once

#include <clap/clap.h>

namespace wclap_test_plugins::voice_info
{

const clap_plugin_descriptor_t& descriptor() noexcept;
bool entryInit(const char* path);
void entryDeinit();
const void* entryGetFactory(const char* factoryId);

} // namespace wclap_test_plugins::voice_info

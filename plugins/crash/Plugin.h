#pragma once

#include <clap/clap.h>

namespace wclap_test_plugins::crash
{

bool entryInit(const char* path);
void entryDeinit();
const void* entryGetFactory(const char* factoryId);

} // namespace wclap_test_plugins::crash

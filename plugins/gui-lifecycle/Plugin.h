#pragma once

#include <clap/clap.h>

namespace example::runtime_example::gui
{

enum class Command : uint8_t
{
    ready = 1,
};

const clap_plugin_descriptor_t& descriptor() noexcept;
bool entryInit(const char* path);
void entryDeinit();
const void* entryGetFactory(const char* factoryId);

} // namespace example::runtime_example::gui

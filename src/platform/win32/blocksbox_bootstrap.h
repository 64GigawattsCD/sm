#ifndef ZELDA3_PLATFORM_WIN32_BLOCKSBOX_BOOTSTRAP_H_
#define ZELDA3_PLATFORM_WIN32_BLOCKSBOX_BOOTSTRAP_H_

#include <stdbool.h>
#include <stddef.h>

bool BlocksBoxBootstrap_ResolveRomPath(const char *preferred_rom_path, char *dst, size_t dst_size);
bool BlocksBoxBootstrap_RunImporter(const char *rom_path, const char *output_dir);
bool BlocksBoxBootstrap_GetAbsolutePath(const char *src, char *dst, size_t dst_size);
bool BlocksBoxBootstrap_GetExecutableRelativePath(const char *relative_path, char *dst, size_t dst_size);

#endif  // ZELDA3_PLATFORM_WIN32_BLOCKSBOX_BOOTSTRAP_H_

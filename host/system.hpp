#pragma once

#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <iterator>

namespace portablekit {

struct ProfilePaths {
    std::filesystem::path disc_image;    // UMD ISO; empty disables disc0:
    std::filesystem::path memory_stick;  // host directory backing ms0:
    std::filesystem::path disc_overlay;  // files read in place of the disc's; may not exist
};

// Installs the kernel and HLE modules, binds logging stubs for the remaining
// imports (unless the STRICT_HLE variable is set) and prepares the loader
// thread that runs module_start.
void install_system(psprecomp::Runtime &runtime, const psprecomp::Elf32Image &elf, const ProfilePaths &paths);

// The host directory that backs ms0:, where the game's saves live under
// PSP/SAVEDATA. This is the only place that decides it; the rest of the host
// receives the result, so moving saves to a per-user location changes only
// this function.
[[nodiscard]] std::filesystem::path memory_stick_directory(const std::filesystem::path &game_dir);

// The host directory of files the game reads in place of the disc's
// (disc_overlay in hle_io.cpp): a modification that leaves the image as it is.
[[nodiscard]] std::filesystem::path disc_overlay_directory(const std::filesystem::path &game_dir);

} // namespace portablekit

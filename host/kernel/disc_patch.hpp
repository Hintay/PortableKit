#pragma once

// A .pkpatch file: changes to one file of the game's disc, read by the disc
// overlay (hle_io.cpp) in place of that file's bytes. It records which file
// it was made from (size and SHA-256), so a patch made from one edition of a
// disc is never applied to another.
//
// Little-endian:
//   "PKPATCH1"
//   u64 size of the file it was made from
//   32 bytes, that file's SHA-256
//   u64 size of the patched file
//   u32 range count
//   per range, in increasing offset order and not overlapping:
//     u64 offset, u32 length, that many bytes
//
// Bytes no range covers are the original file's, and past its end, zero.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace portablekit::disc_patch {

inline constexpr const char *kSuffix = ".pkpatch";

struct Range {
    std::uint64_t offset{};
    std::vector<std::uint8_t> bytes;
};

struct Patch {
    std::uint64_t base_size{};
    std::string base_sha256;  // lower-case hex
    std::uint64_t size{};
    std::vector<Range> ranges;
};

// Empty when the file is missing or is not a valid patch.
[[nodiscard]] std::optional<Patch> read(const std::filesystem::path &path);
// Sorts the ranges and writes the patch. Throws std::runtime_error.
void write(const std::filesystem::path &path, Patch patch);

// Reads `base(offset, destination, length)` -> bytes read, from the original file.
using BaseReader = std::function<std::size_t(std::uint64_t, std::uint8_t *, std::size_t)>;

// The patched file's bytes at [offset, offset + length): how many there are.
std::size_t read_patched(const Patch &patch, std::uint64_t offset, std::uint8_t *destination, std::size_t length,
                         const BaseReader &base);

// SHA-256 (hex) of `size` bytes read through `base`.
[[nodiscard]] std::string hash_base(std::uint64_t size, const BaseReader &base);

} // namespace portablekit::disc_patch

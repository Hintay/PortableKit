#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace portablekit {

// The bytes of a disc image, whichever file holds them: a plain .iso, or a
// CHD (MAME's compressed disc format) made from one, as `chdman createdvd`
// makes them for PSP discs. A CHD is decompressed a hunk at a time as it is
// read; nothing is unpacked to disk.
class DiscReader {
public:
    virtual ~DiscReader() = default;
    // The size of the image the file holds, uncompressed.
    [[nodiscard]] virtual std::uint64_t size_bytes() const = 0;
    // Reads up to output.size() bytes at an offset in the image.
    virtual std::size_t read(std::uint64_t offset, std::span<std::uint8_t> output) = 0;
};

// Whether a file is a CHD, by its signature.
[[nodiscard]] bool is_chd_file(const std::filesystem::path &path);

// Opens an image of either kind. Throws psprecomp::Error, saying what is
// wrong when a CHD is not one this can read.
[[nodiscard]] std::unique_ptr<DiscReader> open_disc_reader(const std::filesystem::path &path);

} // namespace portablekit

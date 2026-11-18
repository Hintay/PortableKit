#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include <array>

namespace psprecomp {

// SHA-256 of data given a piece at a time.
class Sha256 {
public:
    Sha256();
    void update(std::span<const std::uint8_t> bytes);
    // The digest as lower-case hex. The object is spent afterwards.
    [[nodiscard]] std::string finish();

private:
    std::array<std::uint32_t, 8> h_;
    std::array<std::uint8_t, 64> block_{};
    std::size_t filled_{};
    std::uint64_t length_{};
};

[[nodiscard]] std::string sha256_bytes(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string sha256_file(const std::filesystem::path &path);

} // namespace psprecomp

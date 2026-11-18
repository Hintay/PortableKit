#include "kernel/disc_patch.hpp"

#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace portablekit::disc_patch {
namespace {

constexpr char kMagic[8] = {'P', 'K', 'P', 'A', 'T', 'C', 'H', '1'};

std::optional<std::uint64_t> read_le(std::istream &in, int bytes) {
    std::uint64_t value = 0u;
    for (int i = 0; i < bytes; ++i) {
        const int c = in.get();
        if (c == std::char_traits<char>::eof()) return std::nullopt;
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(c)) << (8 * i);
    }
    return value;
}

void write_le(std::ostream &out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
}

std::string to_hex(const std::uint8_t *bytes, std::size_t count) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (std::size_t i = 0; i < count; ++i) {
        text.push_back(kDigits[bytes[i] >> 4u]);
        text.push_back(kDigits[bytes[i] & 0xFu]);
    }
    return text;
}

bool from_hex(const std::string &text, std::uint8_t *bytes, std::size_t count) {
    if (text.size() != count * 2u) return false;
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < count; ++i) {
        const int high = digit(text[i * 2u]);
        const int low = digit(text[i * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

} // namespace

std::optional<Patch> read(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    char magic[sizeof kMagic]{};
    if (!in.read(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof kMagic) != 0) return std::nullopt;
    Patch patch;
    const auto base_size = read_le(in, 8);
    std::uint8_t digest[32]{};
    if (!base_size || !in.read(reinterpret_cast<char *>(digest), sizeof digest)) return std::nullopt;
    const auto size = read_le(in, 8);
    const auto count = read_le(in, 4);
    if (!size || !count) return std::nullopt;
    patch.base_size = *base_size;
    patch.base_sha256 = to_hex(digest, sizeof digest);
    patch.size = *size;
    std::uint64_t previous_end = 0u;
    for (std::uint64_t i = 0; i < *count; ++i) {
        const auto offset = read_le(in, 8);
        const auto length = read_le(in, 4);
        if (!offset || !length || *offset < previous_end || *offset + *length > patch.size) return std::nullopt;
        Range range{*offset, std::vector<std::uint8_t>(static_cast<std::size_t>(*length))};
        if (!in.read(reinterpret_cast<char *>(range.bytes.data()), static_cast<std::streamsize>(range.bytes.size())))
            return std::nullopt;
        previous_end = *offset + *length;
        patch.ranges.push_back(std::move(range));
    }
    return patch;
}

void write(const std::filesystem::path &path, Patch patch) {
    std::sort(patch.ranges.begin(), patch.ranges.end(),
              [](const Range &a, const Range &b) { return a.offset < b.offset; });
    std::uint64_t previous_end = 0u;
    for (const Range &range : patch.ranges) {
        if (range.offset < previous_end || range.offset + range.bytes.size() > patch.size)
            throw std::runtime_error("disc patch ranges overlap or pass the end of the file");
        previous_end = range.offset + range.bytes.size();
    }
    std::uint8_t digest[32]{};
    if (!from_hex(patch.base_sha256, digest, sizeof digest)) throw std::runtime_error("disc patch: bad SHA-256");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.write(kMagic, sizeof kMagic);
    write_le(out, patch.base_size, 8);
    out.write(reinterpret_cast<const char *>(digest), sizeof digest);
    write_le(out, patch.size, 8);
    write_le(out, patch.ranges.size(), 4);
    for (const Range &range : patch.ranges) {
        write_le(out, range.offset, 8);
        write_le(out, range.bytes.size(), 4);
        out.write(reinterpret_cast<const char *>(range.bytes.data()), static_cast<std::streamsize>(range.bytes.size()));
    }
    out.close();
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::size_t read_patched(const Patch &patch, std::uint64_t offset, std::uint8_t *destination, std::size_t length,
                         const BaseReader &base) {
    if (offset >= patch.size) return 0u;
    length = static_cast<std::size_t>(std::min<std::uint64_t>(length, patch.size - offset));
    std::size_t from_base = 0u;
    if (offset < patch.base_size)
        from_base = base(offset, destination,
                         static_cast<std::size_t>(std::min<std::uint64_t>(length, patch.base_size - offset)));
    std::fill(destination + from_base, destination + length, std::uint8_t{0});
    const std::uint64_t end = offset + length;
    // The first range that ends after offset.
    auto it = std::upper_bound(patch.ranges.begin(), patch.ranges.end(), offset,
                               [](std::uint64_t at, const Range &range) { return at < range.offset + range.bytes.size(); });
    for (; it != patch.ranges.end() && it->offset < end; ++it) {
        const std::uint64_t first = std::max(offset, it->offset);
        const std::uint64_t last = std::min<std::uint64_t>(end, it->offset + it->bytes.size());
        std::copy_n(it->bytes.data() + (first - it->offset), last - first, destination + (first - offset));
    }
    return length;
}

std::string hash_base(std::uint64_t size, const BaseReader &base) {
    psprecomp::Sha256 sha;
    std::vector<std::uint8_t> buffer(1u << 20);
    for (std::uint64_t at = 0u; at < size;) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size - at));
        const std::size_t got = base(at, buffer.data(), want);
        if (got == 0u) break;
        sha.update(std::span<const std::uint8_t>(buffer.data(), got));
        at += got;
    }
    return sha.finish();
}

} // namespace portablekit::disc_patch

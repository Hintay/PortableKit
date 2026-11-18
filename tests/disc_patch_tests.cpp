// The disc overlay's patch format (host/kernel/disc_patch.hpp) and the
// piecewise SHA-256 it records the original file with: a patch reads back as
// the file it was made from with its ranges in place, and survives a write
// and a read.

#include "kernel/disc_patch.hpp"

#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <cstdio>
#include <span>
#include <filesystem>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char *what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++g_failures;
}

} // namespace

int main() {
    namespace patch = portablekit::disc_patch;
    std::vector<std::uint8_t> base(5000);
    for (std::size_t i = 0; i < base.size(); ++i) base[i] = static_cast<std::uint8_t>(i * 13u + 1u);

    // Hashing in pieces of every size gives the whole-buffer hash.
    const std::string whole = psprecomp::sha256_bytes(base);
    bool pieces_agree = true;
    for (std::size_t step : {1u, 7u, 63u, 64u, 65u, 1000u, 4999u}) {
        psprecomp::Sha256 sha;
        for (std::size_t at = 0; at < base.size(); at += step)
            sha.update(std::span<const std::uint8_t>(base.data() + at, std::min(step, base.size() - at)));
        pieces_agree = pieces_agree && sha.finish() == whole;
    }
    check(pieces_agree, "SHA-256 in pieces matches the whole");
    check(psprecomp::Sha256().finish() == psprecomp::sha256_bytes({}), "SHA-256 of nothing matches");

    const patch::BaseReader reader = [&base](std::uint64_t at, std::uint8_t *out, std::size_t n) {
        if (at >= base.size()) return std::size_t{0};
        const std::size_t count = std::min<std::size_t>(n, base.size() - static_cast<std::size_t>(at));
        std::copy_n(base.data() + at, count, out);
        return count;
    };
    patch::Patch original;
    original.base_size = base.size();
    original.base_sha256 = patch::hash_base(base.size(), reader);
    check(original.base_sha256 == whole, "hash_base hashes the original");
    original.size = 6144u;
    original.ranges.push_back({6000u, std::vector<std::uint8_t>(100u, 0xEEu)});
    original.ranges.push_back({10u, {1u, 2u, 3u}});

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "portablekit_disc_patch_test.pkpatch";
    patch::write(file, original);
    const auto read = patch::read(file);
    std::filesystem::remove(file);
    check(read && read->base_size == original.base_size && read->base_sha256 == original.base_sha256 &&
              read->size == original.size && read->ranges.size() == 2u && read->ranges[0].offset == 10u,
          "a patch reads back as written, ranges in order");
    if (!read) return 1;

    std::vector<std::uint8_t> expected(base);
    expected.resize(original.size, 0u);
    std::copy_n(original.ranges[1].bytes.data(), 3u, expected.data() + 10u);
    std::fill_n(expected.data() + 6000u, 100u, 0xEEu);
    bool reads_agree = true;
    for (std::uint64_t offset : {0u, 9u, 11u, 4990u, 5000u, 5999u, 6100u, 6143u}) {
        std::vector<std::uint8_t> out(300u, 0x55u);
        const std::size_t got = patch::read_patched(*read, offset, out.data(), out.size(), reader);
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), original.size - offset));
        reads_agree = reads_agree && got == want &&
                      std::equal(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(got), expected.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    check(reads_agree, "reads give the original with the ranges in place, zero past its end");
    std::uint8_t byte = 0u;
    check(patch::read_patched(*read, original.size, &byte, 1u, reader) == 0u, "nothing is read past the patched size");

    std::printf(g_failures == 0 ? "all disc patch tests passed\n" : "%d disc patch test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

#include "kernel/disc_reader.hpp"

#include "psprecomp/common.hpp"

#include <libchdr/chd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace portablekit {
namespace {

constexpr char kChdSignature[8] = {'M', 'C', 'o', 'm', 'p', 'r', 'H', 'D'};
// A PSP disc's sector: what a DVD-type CHD stores per unit. A CD-type CHD
// (chdman createcd) stores 2448-byte frames instead and is not a PSP disc.
constexpr std::uint32_t kSectorBytes = 2048u;

class IsoReader final : public DiscReader {
public:
    explicit IsoReader(const std::filesystem::path &path) : file_(path, std::ios::binary) {
        if (!file_) throw psprecomp::Error("Cannot open disc image: " + path.string());
        size_ = std::filesystem::file_size(path);
    }
    [[nodiscard]] std::uint64_t size_bytes() const override { return size_; }
    std::size_t read(std::uint64_t offset, std::span<std::uint8_t> output) override {
        if (offset >= size_) return 0u;
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), size_ - offset));
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(reinterpret_cast<char *>(output.data()), static_cast<std::streamsize>(count));
        return static_cast<std::size_t>(file_.gcount());
    }

private:
    std::ifstream file_;
    std::uint64_t size_{};
};

// libchdr reads the file through these, so a path is opened the way the
// rest of the host opens one (wide on Windows), not by fopen.
struct HostFile {
    std::ifstream stream;
    std::uint64_t size{};
};

std::uint64_t host_size(void *file) { return static_cast<HostFile *>(file)->size; }

std::size_t host_read(void *buffer, std::size_t size, std::size_t count, void *file) {
    auto &stream = static_cast<HostFile *>(file)->stream;
    if (size == 0u || count == 0u) return 0u;
    stream.read(static_cast<char *>(buffer), static_cast<std::streamsize>(size * count));
    const auto got = static_cast<std::size_t>(stream.gcount());
    stream.clear();
    return got / size;
}

int host_close(void *) { return 0; }  // the reader owns the stream

int host_seek(void *file, std::int64_t offset, int whence) {
    auto &stream = static_cast<HostFile *>(file)->stream;
    stream.clear();
    const std::ios::seekdir dir = whence == SEEK_SET ? std::ios::beg : whence == SEEK_CUR ? std::ios::cur : std::ios::end;
    stream.seekg(static_cast<std::streamoff>(offset), dir);
    return stream ? 0 : -1;
}

constexpr core_file_callbacks kHostCallbacks{host_size, host_read, host_close, host_seek};

class ChdReader final : public DiscReader {
public:
    explicit ChdReader(const std::filesystem::path &path) : name_(path.filename().string()) {
        file_.stream.open(path, std::ios::binary);
        if (!file_.stream) throw psprecomp::Error("Cannot open disc image: " + path.string());
        file_.size = std::filesystem::file_size(path);
        const chd_error error = chd_open_core_file_callbacks(&kHostCallbacks, &file_, CHD_OPEN_READ, nullptr, &chd_);
        if (error == CHDERR_REQUIRES_PARENT)
            throw psprecomp::Error(name_ + " is a CHD that needs a parent image; make a standalone CHD of the disc.");
        if (error != CHDERR_NONE)
            throw psprecomp::Error(name_ + " is not a CHD this build can read (" + chd_error_string(error) + ").");
        const chd_header *header = chd_get_header(chd_);
        hunk_bytes_ = header->hunkbytes;
        hunks_ = header->totalhunks;
        size_ = header->logicalbytes;
        if (header->unitbytes != kSectorBytes || hunk_bytes_ == 0u || hunk_bytes_ % kSectorBytes != 0u) {
            chd_close(chd_);
            throw psprecomp::Error(name_ + " is a CD image in CHD form, not a PSP disc's. Make the CHD from the "
                                           ".iso with chdman createdvd.");
        }
        hunk_.resize(hunk_bytes_);
    }
    ~ChdReader() override { chd_close(chd_); }
    ChdReader(const ChdReader &) = delete;
    ChdReader &operator=(const ChdReader &) = delete;

    [[nodiscard]] std::uint64_t size_bytes() const override { return size_; }

    std::size_t read(std::uint64_t offset, std::span<std::uint8_t> output) override {
        if (offset >= size_) return 0u;
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), size_ - offset));
        std::size_t done = 0u;
        while (done < count) {
            const std::uint64_t at = offset + done;
            const auto hunk = static_cast<std::uint32_t>(at / hunk_bytes_);
            const std::size_t within = static_cast<std::size_t>(at % hunk_bytes_);
            const std::size_t take = std::min<std::size_t>(hunk_bytes_ - within, count - done);
            if (within == 0u && take == hunk_bytes_ && hunk != cached_) {
                // A whole hunk: straight into the caller's buffer.
                if (!decode(hunk, output.data() + done)) break;
            } else {
                if (hunk != cached_) {
                    if (!decode(hunk, hunk_.data())) break;
                    cached_ = hunk;
                }
                std::memcpy(output.data() + done, hunk_.data() + within, take);
            }
            done += take;
        }
        return done;
    }

private:
    bool decode(std::uint32_t hunk, std::uint8_t *destination) {
        if (hunk >= hunks_) return false;
        const chd_error error = chd_read(chd_, hunk, destination);
        if (error == CHDERR_NONE) return true;
        if (!reported_) {
            std::cerr << "[disc] " << name_ << ": hunk " << hunk << " cannot be read (" << chd_error_string(error)
                      << ")\n";
            reported_ = true;
        }
        return false;
    }

    std::string name_;
    HostFile file_;
    chd_file *chd_{};
    std::uint32_t hunk_bytes_{};
    std::uint32_t hunks_{};
    std::uint64_t size_{};
    std::vector<std::uint8_t> hunk_;
    std::uint32_t cached_{0xFFFFFFFFu};
    bool reported_{};
};

} // namespace

bool is_chd_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    char signature[sizeof kChdSignature]{};
    return in.read(signature, sizeof signature) && std::memcmp(signature, kChdSignature, sizeof signature) == 0;
}

std::unique_ptr<DiscReader> open_disc_reader(const std::filesystem::path &path) {
    if (is_chd_file(path)) return std::make_unique<ChdReader>(path);
    return std::make_unique<IsoReader>(path);
}

} // namespace portablekit

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>

namespace sqlitex {
namespace test {

// ============================================================================
// TempFile — RAII helper that creates a unique temporary path and removes
// the file on destruction.
//
// The path must be unique across processes as well as threads: ctest runs
// each test case as a separate process (often in parallel), so a simple
// per-process counter would collide and tests would clobber each other's
// files. We therefore combine a high-resolution timestamp, a random value
// and a process-local atomic counter.
// ============================================================================

class TempFile {
    public:
    TempFile() {
        static std::atomic<unsigned long long> counter{0};
        const auto now = std::chrono::high_resolution_clock::now()
                             .time_since_epoch()
                             .count();
        std::random_device rd;
        const std::string unique = std::to_string(now) + "_" +
                                   std::to_string(rd()) + "_" +
                                   std::to_string(counter.fetch_add(1));
        auto p = std::filesystem::temp_directory_path() /
                 ("sqlitex_test_" + unique + ".db");
        path_ = p.string();
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const std::string& path() const {
        return path_;
    }

    private:
    std::string path_;
};

} // namespace test
} // namespace sqlitex


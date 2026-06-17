#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace sqlitex {
namespace test {

// ============================================================================
// TempFile — RAII helper that creates a unique temporary path and removes
// the file on destruction.
// ============================================================================

class TempFile {
    public:
    TempFile() {
        auto p = std::filesystem::temp_directory_path() /
                 ("sqlitex_test_" + std::to_string(counter_++) + ".db");
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
    static inline int counter_ = 0;
};

} // namespace test
} // namespace sqlitex

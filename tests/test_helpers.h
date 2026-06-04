#pragma once

#include <cstdio>
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
        path_ = "/tmp/sqlitex_test_" + std::to_string(counter_++) + ".db";
    }
    ~TempFile() {
        std::remove(path_.c_str());
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

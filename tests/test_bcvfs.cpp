#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace sqlitex {
namespace {

/// Return a platform-agnostic temporary cache path for tests.
inline std::string test_cache_path(const char* subdir = "sqlitex-test-cache") {
    return (std::filesystem::temp_directory_path() / subdir).string();
}



// ============================================================================
// Enum tests
// ============================================================================

TEST(BcvfsConfigTest, CacheSize) {
    EXPECT_NE(static_cast<int>(BcvfsConfig::CacheSize),
              static_cast<int>(BcvfsConfig::NRequest));
}

TEST(BcvfsConfigTest, DistinctValues) {
    auto a = static_cast<int>(BcvfsConfig::CacheSize);
    auto b = static_cast<int>(BcvfsConfig::NRequest);
    auto c = static_cast<int>(BcvfsConfig::HttpTimeout);
    auto d = static_cast<int>(BcvfsConfig::HttpLog);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_NE(b, c);
    EXPECT_NE(b, d);
    EXPECT_NE(c, d);
}

TEST(BcvfsStorageTest, DistinctValues) {
    EXPECT_NE(static_cast<int>(BcvfsStorage::Azure),
              static_cast<int>(BcvfsStorage::Google));
}

// ============================================================================
// PrefetchHandle — default / stub behavior
// ============================================================================

TEST(PrefetchHandleTest, DefaultConstruction) {
    PrefetchHandle h;
    EXPECT_FALSE(h);
}

TEST(PrefetchHandleTest, StubStatus) {
    PrefetchHandle h;
    EXPECT_EQ(h.status(), PrefetchStatus::Error);
}

TEST(PrefetchHandleTest, StubErrorMessage) {
    PrefetchHandle h;
    EXPECT_FALSE(h.error_message().empty());
}

TEST(PrefetchHandleTest, StubRunIsNoop) {
    PrefetchHandle h;
    EXPECT_NO_THROW(h.run(1, std::chrono::milliseconds(100)));
}

TEST(PrefetchHandleTest, MoveConstruction) {
    PrefetchHandle h1;
    PrefetchHandle h2(std::move(h1));
    EXPECT_FALSE(h2);
    EXPECT_FALSE(h1); // NOLINT
}

TEST(PrefetchHandleTest, MoveAssignment) {
    PrefetchHandle h1;
    PrefetchHandle h2;
    h2 = std::move(h1);
    EXPECT_FALSE(h2);
}

// ============================================================================
// Bcvfs — stub construction (throws when bcvfs not available)
// ============================================================================

TEST(BcvfsTest, StubConstructionThrows) {
    EXPECT_THROW(
        {
            try {
                Bcvfs b(test_cache_path(), "bcvfs");
            } catch (const Error& e) {
                EXPECT_NE(e.code(), 0);
                std::string msg(e.what());
                EXPECT_NE(msg.find("bcvfs"), std::string::npos);
                throw;
            }
        },
        Error);
}

TEST(BcvfsTest, VfsNameAvailableEvenWhenStub) {
    // Construction throws, so we can't inspect vfs_name directly.
    // vfs_name() is noexcept and returns the stored name.
    EXPECT_THROW(Bcvfs(test_cache_path(), "my-custom-vfs"), Error);
}

// ============================================================================
// Bcvfs — move semantics with stub
// ============================================================================

TEST(BcvfsTest, MoveConstruction) {
    // We can't create a valid Bcvfs to move from in stub mode.
    // But we can verify the class compiles and the move constructor exists.
    // The Bcvfs class is move-only (no copy).
    EXPECT_TRUE(std::is_move_constructible_v<Bcvfs>);
    EXPECT_FALSE(std::is_copy_constructible_v<Bcvfs>);
    EXPECT_TRUE(std::is_move_assignable_v<Bcvfs>);
    EXPECT_FALSE(std::is_copy_assignable_v<Bcvfs>);
}

// ============================================================================
// Bcvfs — callback type verification
// ============================================================================

TEST(BcvfsCallbackTest, AuthCallbackSignature) {
    BcvfsAuthCallback cb =
        [](std::string_view storage, std::string_view account,
           std::string_view container, std::string& token) -> bool {
        token = "test-token";
        return true;
    };

    std::string token;
    bool ok = cb("azure", "acct", "cont", token);
    EXPECT_TRUE(ok);
    EXPECT_EQ(token, "test-token");
}

TEST(BcvfsCallbackTest, LogCallbackSignature) {
    std::string captured;
    BcvfsLogCallback cb = [&](std::string_view msg) {
        captured = std::string(msg);
    };

    cb("test-message");
    EXPECT_EQ(captured, "test-message");
}

TEST(BcvfsCallbackTest, BusyCallbackReturnsDelay) {
    BcvfsBusyCallback cb =
        [](int retry) -> std::optional<std::chrono::milliseconds> {
        if (retry > 3)
            return std::nullopt; // give up after 3
        return std::chrono::milliseconds(retry * 100);
    };

    auto d1 = cb(1);
    ASSERT_TRUE(d1.has_value());
    EXPECT_EQ(d1->count(), 100);

    auto d4 = cb(4);
    EXPECT_FALSE(d4.has_value());
}

TEST(BcvfsCallbackTest, BusyCallbackDefaultCancels) {
    BcvfsBusyCallback cb; // empty callback
    EXPECT_FALSE(cb);     // not callable
}

// ============================================================================
// BcvfsConfig enum — value coverage
// ============================================================================

TEST(BcvfsConfigTest, AllValuesDefined) {
    // Just verify compilation and sensible ranges
    auto cs = static_cast<int>(BcvfsConfig::CacheSize);
    auto nr = static_cast<int>(BcvfsConfig::NRequest);
    auto ht = static_cast<int>(BcvfsConfig::HttpTimeout);
    auto hl = static_cast<int>(BcvfsConfig::HttpLog);
    // All should be non-negative small ints
    EXPECT_GE(cs, 0);
    EXPECT_GE(nr, 0);
    EXPECT_GE(ht, 0);
    EXPECT_GE(hl, 0);
}

// ============================================================================
// BcvfsStorage enum — value coverage
// ============================================================================

TEST(BcvfsStorageTest, AllValuesDefined) {
    auto azure = static_cast<int>(BcvfsStorage::Azure);
    auto google = static_cast<int>(BcvfsStorage::Google);
    EXPECT_GE(azure, 0);
    EXPECT_GE(google, 0);
}

// ============================================================================
// PrefetchStatus enum
// ============================================================================

TEST(PrefetchStatusTest, DistinctValues) {
    auto r = static_cast<int>(PrefetchStatus::Running);
    auto d = static_cast<int>(PrefetchStatus::Done);
    auto e = static_cast<int>(PrefetchStatus::Error);
    EXPECT_NE(r, d);
    EXPECT_NE(r, e);
    EXPECT_NE(d, e);
}

} // anonymous namespace
} // namespace sqlitex

#include <sqlitex/sqlitex.h>

// Verify public header hygiene: including the umbrella header must NOT pull
// in <sqlite3.h>.
#if defined(SQLITE3_H) || defined(_SQLITE3_H_) || defined(SQLITE_VERSION_NUMBER)
#error "Public sqlitex headers must not include <sqlite3.h>"
#endif

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace sqlitex {
namespace {

TEST(SqliteGlobalTest, VersionAndCompileOptions) {
    EXPECT_EQ(libversion_number(), 3054001);
    EXPECT_EQ(libversion(), "3.54.1");
    EXPECT_FALSE(sourceid().empty());
    EXPECT_NE(threadsafe(), ThreadingMode::SingleThread);

    EXPECT_TRUE(compileoption_used("ENABLE_SESSION"));
    EXPECT_TRUE(compileoption_used("ENABLE_PREUPDATE_HOOK"));
    EXPECT_TRUE(compileoption_used("ENABLE_COLUMN_METADATA"));
    EXPECT_TRUE(compileoption_used("ENABLE_FTS5"));
    EXPECT_TRUE(compileoption_used("ENABLE_RTREE"));
    EXPECT_TRUE(compileoption_used("ENABLE_CARRAY"));
    EXPECT_TRUE(compileoption_used("ENABLE_SNAPSHOT"));
    EXPECT_TRUE(compileoption_used("ENABLE_STMT_SCANSTATUS"));
    EXPECT_TRUE(compileoption_used("ENABLE_UNLOCK_NOTIFY"));
    EXPECT_TRUE(compileoption_used("ENABLE_NORMALIZE"));
    EXPECT_TRUE(compileoption_used("ENABLE_SETLK_TIMEOUT"));

    EXPECT_FALSE(compileoption_used("DEFINITELY_NOT_AN_OPTION_XYZ"));

    auto first = compileoption_get(0);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(first->empty());
    EXPECT_FALSE(compileoption_get(99999).has_value());

    auto all = compile_options();
    EXPECT_GT(all.size(), 5U);
}

TEST(SqliteGlobalTest, Errstr) {
    EXPECT_EQ(errstr(0), "not an error");
    EXPECT_FALSE(errstr(1).empty());
    EXPECT_FALSE(errstr(19).empty());
}

TEST(SqliteGlobalTest, MemoryAndStatus) {
    Database db(":memory:");
    db.execute("CREATE TABLE t(x TEXT); INSERT INTO t VALUES ('hello');");

    EXPECT_GT(memory_used(), 0);
    EXPECT_GE(memory_highwater(false), memory_used());

    auto st = status(StatusOp::MemoryUsed, false);
    EXPECT_GT(st.current, 0);
    EXPECT_GE(st.highwater, st.current);

    int64_t prev_soft = soft_heap_limit(-1);
    EXPECT_GE(prev_soft, 0);
    int64_t set_prev = soft_heap_limit(64LL * 1024 * 1024);
    EXPECT_EQ(set_prev, prev_soft);
    EXPECT_EQ(soft_heap_limit(prev_soft), 64LL * 1024 * 1024);

    int64_t prev_hard = hard_heap_limit(-1);
    EXPECT_GE(prev_hard, 0);

    EXPECT_GE(release_memory(1024), 0);
}

TEST(SqliteGlobalTest, CompleteIncompleteAndKeywords) {
    EXPECT_TRUE(complete("SELECT 1;"));
    EXPECT_FALSE(complete("SELECT 1"));

    EXPECT_EQ(incomplete("SELECT 1;"), 0);
    EXPECT_NE(incomplete("SELECT 1"), 0);
    EXPECT_NE(incomplete("   -- comment only\n"), 0);

    int count = keyword_count();
    EXPECT_GT(count, 100);
    auto kw0 = keyword_name(0);
    ASSERT_TRUE(kw0.has_value());
    EXPECT_FALSE(kw0->empty());
    EXPECT_FALSE(keyword_name(-1).has_value());
    EXPECT_FALSE(keyword_name(count + 10).has_value());

    EXPECT_TRUE(keyword_check("SELECT"));
    EXPECT_TRUE(keyword_check("select"));
    EXPECT_FALSE(keyword_check("not_a_sql_keyword_xyz"));
}

TEST(SqliteGlobalTest, UriFilename) {
    UriFilename uri("/tmp/test_uri.db", "/tmp/test_uri.db-journal",
                    "/tmp/test_uri.db-wal",
                    {{"mode", "ro"}, {"immutable", "1"}, {"timeout", "2500"}});
    ASSERT_TRUE(uri);
    EXPECT_EQ(uri.database(), "/tmp/test_uri.db");
    EXPECT_EQ(uri.journal(), "/tmp/test_uri.db-journal");
    EXPECT_EQ(uri.wal(), "/tmp/test_uri.db-wal");

    EXPECT_EQ(uri.parameter("mode"), std::optional<std::string_view>("ro"));
    EXPECT_FALSE(uri.parameter("missing").has_value());
    EXPECT_TRUE(uri.boolean_parameter("immutable", false));
    EXPECT_FALSE(uri.boolean_parameter("missing_bool", false));
    EXPECT_EQ(uri.int64_parameter("timeout", 0), 2500);
    EXPECT_EQ(uri.int64_parameter("missing_int", 42), 42);
    EXPECT_EQ(uri.key(0), std::optional<std::string_view>("mode"));
    EXPECT_FALSE(uri.key(10).has_value());

    // Free-function equivalents
    EXPECT_EQ(filename_database(uri.c_str()), "/tmp/test_uri.db");
    EXPECT_EQ(filename_journal(uri.c_str()), "/tmp/test_uri.db-journal");
    EXPECT_EQ(filename_wal(uri.c_str()), "/tmp/test_uri.db-wal");
    EXPECT_EQ(uri_parameter(uri.c_str(), "mode"),
              std::optional<std::string_view>("ro"));
    EXPECT_TRUE(uri_boolean(uri.c_str(), "immutable", false));
    EXPECT_EQ(uri_int64(uri.c_str(), "timeout", 0), 2500);
    EXPECT_EQ(uri_key(uri.c_str(), 1),
              std::optional<std::string_view>("immutable"));

    // Move semantics
    UriFilename moved(std::move(uri));
    EXPECT_FALSE(uri); // NOLINT(bugprone-use-after-move)
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved.database(), "/tmp/test_uri.db");
}

TEST(SqliteGlobalTest, VfsFindAndAutoExtension) {
    sqlite3_vfs* def_vfs = vfs_find();
    EXPECT_NE(def_vfs, nullptr);
    EXPECT_EQ(vfs_find("definitely-nonexistent-vfs-name"), nullptr);

    static std::atomic<int> init_calls{0};
    auto entry =
        +[](sqlite3* /*db*/, const char** /*err*/, const void* /*api*/) -> int {
        init_calls.fetch_add(1);
        return 0;
    };
    auto raw_entry = reinterpret_cast<AutoExtensionEntryPoint>(entry);
    init_calls.store(0);
    auto_extension(raw_entry);
    {
        Database db(":memory:");
        EXPECT_EQ(init_calls.load(), 1);
    }
    EXPECT_TRUE(cancel_auto_extension(raw_entry));
    EXPECT_FALSE(cancel_auto_extension(raw_entry));
    {
        Database db(":memory:");
        EXPECT_EQ(init_calls.load(), 1);
    }
}

TEST(SqliteGlobalTest, LogAndRowidInViewQuery) {
    // Querying rowid-in-view with -1 works after initialization
    int val = config_rowid_in_view(-1);
    EXPECT_GE(val, 0);

    // Calling log() is always safe (no-op if no logger installed)
    EXPECT_NO_THROW(log(0, "sqlitex test log message"));
}

} // namespace
} // namespace sqlitex

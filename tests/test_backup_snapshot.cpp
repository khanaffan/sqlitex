#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace sqlitex {
namespace {

std::filesystem::path make_temp_db_path(const char* tag) {
    static int counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("sqlitex_" + std::string(tag) + "_" + std::to_string(now) + "_" +
            std::to_string(++counter) + ".db");
}

void remove_db_files(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
    std::filesystem::remove(p.string() + "-wal", ec);
    std::filesystem::remove(p.string() + "-shm", ec);
    std::filesystem::remove(p.string() + "-journal", ec);
}

// ============================================================================
// Online Backup API
// ============================================================================

TEST(BackupTest, FullCopyAndProgress) {
    Database src(":memory:");
    src.execute("CREATE TABLE items(id INTEGER PRIMARY KEY, payload TEXT);");
    {
        Database::Transaction tx(src);
        auto ins = src.prepare("INSERT INTO items(payload) VALUES (?);");
        std::string big(256, 'x');
        for (int i = 0; i < 200; ++i) {
            ins.bind(1, big).execute();
            ins.reset();
        }
    }

    Database dst(":memory:");
    Backup bk(dst, src);
    ASSERT_TRUE(bk);

    int callbacks = 0;
    int last_remaining = -1;
    int last_pagecount = 0;
    bk.run(2, [&](int rem, int total) {
        ++callbacks;
        last_remaining = rem;
        last_pagecount = total;
    });

    EXPECT_FALSE(bk); // finished
    EXPECT_GE(callbacks, 1);
    EXPECT_EQ(last_remaining, 0);
    EXPECT_GT(last_pagecount, 0);

    auto count = dst.query_value<int64_t>("SELECT COUNT(*) FROM items;");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 200);
}

TEST(BackupTest, IncrementalStepAndMove) {
    Database src(":memory:");
    src.execute(
        "CREATE TABLE t(v INT); INSERT INTO t VALUES (10), (20), (30);");
    Database dst(":memory:");

    Backup bk1(dst, src);
    Backup bk2(std::move(bk1));
    EXPECT_FALSE(bk1); // NOLINT(bugprone-use-after-move)
    ASSERT_TRUE(bk2);

    bool done = false;
    while (!done) {
        done = bk2.step(1);
        EXPECT_GE(bk2.pagecount(), 1);
        EXPECT_GE(bk2.remaining(), 0);
    }
    EXPECT_EQ(bk2.remaining(), 0);
    bk2.finish();
    EXPECT_FALSE(bk2);

    EXPECT_EQ(dst.query_value<int64_t>("SELECT SUM(v) FROM t;"), 60);
}

TEST(BackupTest, InvalidSchemaThrows) {
    Database src(":memory:");
    Database dst(":memory:");
    EXPECT_THROW(Backup(dst, src, "main", "nonexistent_schema"), Error);
}

// ============================================================================
// Serialize / Deserialize
// ============================================================================

TEST(SerializeTest, RoundTripOwnedAndRawBytes) {
    Database src(":memory:");
    src.execute(
        "CREATE TABLE kv(k TEXT PRIMARY KEY, v INT);"
        "INSERT INTO kv VALUES ('a', 1), ('b', 2);");

    SerializedDatabase img = src.serialize("main");
    ASSERT_TRUE(img);
    EXPECT_GT(img.size(), 0);
    EXPECT_TRUE(img.is_owned());

    std::vector<uint8_t> bytes = img.to_bytes();
    EXPECT_EQ(static_cast<int64_t>(bytes.size()), img.size());

    // Deserialize moving the SerializedDatabase
    Database dst1(":memory:");
    dst1.deserialize("main", std::move(img));
    EXPECT_FALSE(img); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(dst1.query_value<int64_t>("SELECT v FROM kv WHERE k='b';"), 2);
    // Writable because Resizeable was set
    dst1.execute("INSERT INTO kv VALUES ('c', 3);");
    EXPECT_EQ(dst1.query_value<int64_t>("SELECT COUNT(*) FROM kv;"), 3);

    // Deserialize from raw byte vector as ReadOnly
    Database dst2(":memory:");
    dst2.deserialize("main", bytes.data(), static_cast<int64_t>(bytes.size()),
                     DeserializeFlag::FreeOnClose | DeserializeFlag::ReadOnly);
    EXPECT_EQ(dst2.query_value<int64_t>("SELECT v FROM kv WHERE k='a';"), 1);
    EXPECT_THROW(dst2.execute("INSERT INTO kv VALUES ('d', 4);"), Error);
}

TEST(SerializeTest, CopyFromHelper) {
    Database src(":memory:");
    src.execute("CREATE TABLE t(x INT); INSERT INTO t VALUES (99);");
    auto orig = src.serialize();
    auto copy = SerializedDatabase::copy_from(orig.data(), orig.size());
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy.size(), orig.size());

    Database dst(":memory:");
    dst.deserialize("main", std::move(copy));
    EXPECT_EQ(dst.query_value<int64_t>("SELECT x FROM t;"), 99);
}

// ============================================================================
// WAL Checkpointing, WAL Hook & Snapshots
// ============================================================================

TEST(WalAndSnapshotTest, WalHookCheckpointAndSnapshotIsolation) {
    auto db_path = make_temp_db_path("wal_snap");
    remove_db_files(db_path);

    {
        Database db1(db_path.string());
        db1.execute("PRAGMA journal_mode=WAL;");
        db1.wal_autocheckpoint(0); // disable auto-checkpoint so WAL stays alive

        int wal_hook_calls = 0;
        int last_wal_pages = 0;
        db1.set_wal_hook(
            [&](Database& /*d*/, std::string_view schema, int pages) -> int {
                EXPECT_EQ(schema, "main");
                ++wal_hook_calls;
                last_wal_pages = pages;
                return SQLITE_OK;
            });

        db1.execute("CREATE TABLE log(id INTEGER PRIMARY KEY, val INT);");
        db1.execute("INSERT INTO log VALUES (1, 100);");
        EXPECT_GE(wal_hook_calls, 2);
        EXPECT_GT(last_wal_pages, 0);

        // Open a second connection to capture snapshots
        Database db2(db_path.string());

        // Snapshot 1: sees val = 100
        db2.begin_transaction();
        EXPECT_EQ(db2.query_value<int64_t>("SELECT val FROM log WHERE id=1;"),
                  100);
        Snapshot snap1 = Snapshot::get(db2, "main");
        ASSERT_TRUE(snap1);
        db2.commit();

        // Update on db1 to val = 200
        db1.execute("UPDATE log SET val = 200 WHERE id = 1;");

        // Snapshot 2: sees val = 200
        db2.begin_transaction();
        EXPECT_EQ(db2.query_value<int64_t>("SELECT val FROM log WHERE id=1;"),
                  200);
        Snapshot snap2 = Snapshot::get(db2, "main");
        ASSERT_TRUE(snap2);
        db2.commit();

        EXPECT_LT(snap1, snap2);
        EXPECT_GT(snap2, snap1);
        EXPECT_NE(snap1, snap2);
        EXPECT_EQ(Snapshot::compare(snap1, snap1), 0);

        // Move semantics on Snapshot
        Snapshot snap1_moved(std::move(snap1));
        EXPECT_FALSE(snap1); // NOLINT(bugprone-use-after-move)
        ASSERT_TRUE(snap1_moved);

        // Re-open db2 at historical snap1_moved and verify it sees 100!
        db2.begin_transaction();
        snap1_moved.open(db2, "main");
        EXPECT_EQ(db2.query_value<int64_t>("SELECT val FROM log WHERE id=1;"),
                  100);
        db2.commit();

        // Clear WAL hook and run explicit checkpoint
        db1.set_wal_hook({});
        auto res = db1.wal_checkpoint_v2(CheckpointMode::Truncate, "main");
        EXPECT_GE(res.log_frames, 0);
        EXPECT_GE(res.checkpointed_frames, 0);

        // Recover snapshots (valid on WAL DB)
        EXPECT_NO_THROW(Snapshot::recover(db1, "main"));
    }

    remove_db_files(db_path);
}

} // namespace
} // namespace sqlitex

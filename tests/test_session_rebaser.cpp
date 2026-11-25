#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {
namespace {

// ============================================================================
// Session: changeset_size, stream_chunk_size
// ============================================================================

TEST(SessionAdvancedTest, ChangesetSizeAndStreamChunkConfig) {
    Database db(":memory:");
    db.execute("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT);");

    Session sess(db);
    // Enable SQLITE_SESSION_OBJCONFIG_SIZE before attaching tables
    int enable_size = 1;
    sess.set_object_config(SQLITE_SESSION_OBJCONFIG_SIZE, &enable_size);

    // Stream chunk size config
    int prev = sess.set_stream_chunk_size(4096);
    EXPECT_GT(prev, 0);
    EXPECT_EQ(sess.stream_chunk_size(), 4096);

    sess.attach("t");
    EXPECT_EQ(sess.changeset_size(), 0);

    db.execute("INSERT INTO t VALUES (1, 'hello'), (2, 'world');");
    EXPECT_GT(sess.changeset_size(), 0);
}

// ============================================================================
// Changeset apply_v3_stream & Rebaser end-to-end conflict rebase
// ============================================================================

TEST(SessionAdvancedTest, RebaserAndApplyV3Stream) {
    // Two replicas start from the same initial state
    Database db1(":memory:");
    Database db2(":memory:");
    const char* schema =
        "CREATE TABLE kv(id INTEGER PRIMARY KEY, a TEXT, b TEXT);"
        "INSERT INTO kv VALUES (1, 'initA', 'initB');";
    db1.execute(schema);
    db2.execute(schema);

    // Local edit on db1: change row 1 column 'a' -> 'localA'
    Session s_local(db1);
    s_local.attach("kv");
    db1.execute("UPDATE kv SET a = 'localA' WHERE id = 1;");
    Changeset cs_local = s_local.changeset();

    // Concurrent remote edit on db2: change row 1 column 'a' -> 'remoteA'
    Session s_remote(db2);
    s_remote.attach("kv");
    db2.execute("UPDATE kv SET a = 'remoteA' WHERE id = 1;");
    Changeset cs_remote = s_remote.changeset();

    // Apply cs_remote to db1 via apply_v3_stream, omitting on DATA conflict
    // and capturing rebase metadata.
    std::vector<uint8_t> rebase_data;
    size_t read_pos = 0;
    const auto* remote_bytes = static_cast<const uint8_t*>(cs_remote.data());
    const size_t remote_size = static_cast<size_t>(cs_remote.size());

    auto stream_in = [&](void* buf, int& n) -> int {
        size_t avail = remote_size - read_pos;
        size_t to_copy = std::min<size_t>(avail, static_cast<size_t>(n));
        if (to_copy > 0) {
            std::memcpy(buf, remote_bytes + read_pos, to_copy);
            read_pos += to_copy;
        }
        n = static_cast<int>(to_copy);
        return SQLITE_OK;
    };

    int conflict_count = 0;
    int filter_count = 0;
    Changeset::apply_v3_stream(
        db1, stream_in,
        [&](ConflictType type, const ChangesetIterator&) {
            ++conflict_count;
            EXPECT_EQ(type, ConflictType::Data);
            return ChangesetAction::Omit;
        },
        [&](const ChangesetIterator&) {
            ++filter_count;
            return true;
        },
        static_cast<ApplyFlags>(ApplyFlag::NoUpdateLoop), &rebase_data);

    EXPECT_EQ(filter_count, 1);
    EXPECT_EQ(conflict_count, 1);
    EXPECT_FALSE(rebase_data.empty());
    // Because we omitted the remote change, db1 still has 'localA'
    EXPECT_EQ(db1.query_value<std::string>("SELECT a FROM kv WHERE id = 1;"),
              "localA");

    // Configure Rebaser with rebase_data and rebase cs_local so it can be
    // cleanly applied to db2 (which currently has 'remoteA')!
    Rebaser rebaser;
    ASSERT_TRUE(rebaser);
    rebaser.configure(rebase_data);

    Changeset rebased = rebaser.rebase(cs_local);
    ASSERT_TRUE(rebased);

    // Move-test Rebaser and also test rebase_stream
    Rebaser rebaser2(std::move(rebaser));
    EXPECT_FALSE(rebaser); // NOLINT(bugprone-use-after-move)
    ASSERT_TRUE(rebaser2);

    std::vector<uint8_t> streamed_rebased;
    size_t local_pos = 0;
    const auto* local_bytes = static_cast<const uint8_t*>(cs_local.data());
    const size_t local_size = static_cast<size_t>(cs_local.size());
    rebaser2.rebase_stream(
        [&](void* buf, int& n) -> int {
            size_t avail = local_size - local_pos;
            size_t to_copy = std::min<size_t>(avail, static_cast<size_t>(n));
            if (to_copy > 0) {
                std::memcpy(buf, local_bytes + local_pos, to_copy);
                local_pos += to_copy;
            }
            n = static_cast<int>(to_copy);
            return SQLITE_OK;
        },
        [&](const void* data, int n) -> int {
            const auto* p = static_cast<const uint8_t*>(data);
            streamed_rebased.insert(streamed_rebased.end(), p, p + n);
            return SQLITE_OK;
        });
    EXPECT_EQ(static_cast<int>(streamed_rebased.size()), rebased.size());

    // Applying the rebased changeset to db2 succeeds with zero conflicts!
    int db2_conflicts = 0;
    rebased.apply_v2(
        db2,
        [&](ConflictType, const ChangesetIterator&) {
            ++db2_conflicts;
            return ChangesetAction::Abort;
        },
        0);
    EXPECT_EQ(db2_conflicts, 0);
    EXPECT_EQ(db2.query_value<std::string>("SELECT a FROM kv WHERE id = 1;"),
              "localA");
}

// ============================================================================
// Changegroup: patchset mode, set_schema, add_change, programmatic builder
// ============================================================================

TEST(ChangegroupAdvancedTest, ProgrammaticBuilderAndPatchsetMode) {
    Database db(":memory:");
    db.execute(
        "CREATE TABLE items("
        "id INTEGER PRIMARY KEY, "
        "score REAL, "
        "label TEXT, "
        "raw BLOB, "
        "extra TEXT);");

    // 1. Programmatic changeset builder
    Changegroup cg;
    cg.set_schema(db, "main");
    EXPECT_FALSE(cg.is_patchset_mode());

    const uint8_t blob_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    cg.change_begin(ChangeOperation::Insert, "items");
    cg.change_int64(true, 0, 101);
    cg.change_double(true, 1, 3.25);
    cg.change_text(true, 2, "built");
    cg.change_blob(true, 3, blob_data, sizeof(blob_data));
    cg.change_null(true, 4);
    cg.change_finish(false);

    // Also test discarding a change
    cg.change_begin(ChangeOperation::Insert, "items");
    cg.change_int64(true, 0, 999);
    cg.change_finish(true);

    Changeset built = cg.output();
    ASSERT_FALSE(built.empty());

    built.apply(db, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });

    EXPECT_EQ(db.query_value<int64_t>("SELECT COUNT(*) FROM items;"), 1);
    EXPECT_EQ(
        db.query_value<std::string>("SELECT label FROM items WHERE id = 101;"),
        "built");
    EXPECT_DOUBLE_EQ(
        db.query_value<double>("SELECT score FROM items WHERE id = 101;")
            .value(),
        3.25);
    EXPECT_EQ(db.query_value<int64_t>(
                  "SELECT length(raw) FROM items WHERE id = 101;"),
              4);

    // 2. add_change from iterator + patchset mode
    Changegroup pg;
    EXPECT_TRUE(pg.set_patchset_mode(true));
    EXPECT_TRUE(pg.is_patchset_mode());

    auto it = built.begin();
    ASSERT_TRUE(it.next());
    pg.add_change(it);
    Patchset ps = pg.output_patchset();
    ASSERT_FALSE(ps.empty());

    Database db2(":memory:");
    db2.execute(
        "CREATE TABLE items("
        "id INTEGER PRIMARY KEY, "
        "score REAL, "
        "label TEXT, "
        "raw BLOB, "
        "extra TEXT);");
    ps.apply(db2, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });
    EXPECT_EQ(
        db2.query_value<std::string>("SELECT label FROM items WHERE id = 101;"),
        "built");
}

} // namespace
} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION

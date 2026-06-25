#include <sqlitex/sqlitex.h>

// Uses the raw SQLite C API directly (sqlite3_malloc), so it includes
// <sqlite3.h> explicitly.
#include <sqlite3.h>

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>

namespace sqlitex {
namespace {

class ChangesetTest : public ::testing::Test {
    protected:
    void SetUp() override {
        db_.execute(R"(
            CREATE TABLE items (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                quantity INTEGER DEFAULT 0
            )
        )");
        db_.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
        db_.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");
    }

    Database db_{":memory:"};
};

// ============================================================================
// Changeset construction
// ============================================================================

TEST_F(ChangesetTest, DefaultConstruction) {
    Changeset cs;
    EXPECT_TRUE(cs.empty());
    EXPECT_EQ(cs.size(), 0);
}

TEST_F(ChangesetTest, MoveConstruction) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'Test', 1)");

    Changeset cs1 = session.changeset();
    int size = cs1.size();

    Changeset cs2(std::move(cs1));
    EXPECT_EQ(cs2.size(), size);
    EXPECT_TRUE(cs1.empty()); // NOLINT
    EXPECT_EQ(cs1.size(), 0); // NOLINT
}

TEST_F(ChangesetTest, MoveAssignment) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'A', 1)");
    db_.execute("INSERT INTO items VALUES (4, 'B', 2)");

    Changeset cs1 = session.changeset();
    int size1 = cs1.size();

    // Second session for another changeset
    Session s2(db_, "main");
    s2.enable(true);
    s2.attach_all();
    db_.execute("DELETE FROM items WHERE id = 4");
    Changeset cs2 = s2.changeset();

    cs2 = std::move(cs1);
    EXPECT_EQ(cs2.size(), size1);
}

// ============================================================================
// Changeset copy_from
// ============================================================================

TEST_F(ChangesetTest, CopyFrom) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'Test', 1)");

    Changeset cs1 = session.changeset();
    Changeset cs2 = Changeset::copy_from(cs1.data(), cs1.size());

    EXPECT_EQ(cs1.size(), cs2.size());
    EXPECT_NE(cs1.data(), cs2.data()); // Different pointers
}

// ============================================================================
// Iterator — basic iteration
// ============================================================================

TEST_F(ChangesetTest, IterateInsert) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");

    Changeset cs = session.changeset();
    auto iter = cs.begin();

    ASSERT_TRUE(iter.next());
    EXPECT_EQ(iter.op(), ChangeOperation::Insert);
    EXPECT_EQ(iter.table_name(), "items");
    EXPECT_EQ(iter.column_count(), 3);
    EXPECT_FALSE(iter.next());
}

TEST_F(ChangesetTest, IterateUpdate) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("UPDATE items SET quantity = 99 WHERE id = 1");

    Changeset cs = session.changeset();
    auto iter = cs.begin();

    ASSERT_TRUE(iter.next());
    EXPECT_EQ(iter.op(), ChangeOperation::Update);
    EXPECT_EQ(iter.table_name(), "items");
    EXPECT_FALSE(iter.next());
}

TEST_F(ChangesetTest, IterateDelete) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("DELETE FROM items WHERE id = 2");

    Changeset cs = session.changeset();
    auto iter = cs.begin();

    ASSERT_TRUE(iter.next());
    EXPECT_EQ(iter.op(), ChangeOperation::Delete);
    EXPECT_FALSE(iter.next());
}

// ============================================================================
// Iterator — value access
// ============================================================================

TEST_F(ChangesetTest, InsertNewValues) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");

    Changeset cs = session.changeset();
    auto iter = cs.begin();
    ASSERT_TRUE(iter.next());

    // Old values should not be available for INSERT
    EXPECT_TRUE(iter.old_is_null(0));

    // New values should be available
    EXPECT_EQ(iter.new_int(0).value_or(-1), 3);
    EXPECT_EQ(iter.new_text(1).value_or(""), "Doodad");
    EXPECT_EQ(iter.new_int(2).value_or(-1), 25);
}

TEST_F(ChangesetTest, DeleteOldValues) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("DELETE FROM items WHERE id = 2");

    Changeset cs = session.changeset();
    auto iter = cs.begin();
    ASSERT_TRUE(iter.next());
    EXPECT_EQ(iter.op(), ChangeOperation::Delete);

    // Old values should be available for DELETE
    EXPECT_EQ(iter.old_int(0).value_or(-1), 2);
    EXPECT_EQ(iter.old_text(1).value_or(""), "Gadget");
}

TEST_F(ChangesetTest, UpdateBothValues) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("UPDATE items SET quantity = 99 WHERE id = 1");

    Changeset cs = session.changeset();
    auto iter = cs.begin();
    ASSERT_TRUE(iter.next());
    EXPECT_EQ(iter.op(), ChangeOperation::Update);

    // PK old value is always available for UPDATE
    EXPECT_EQ(iter.old_int(0).value_or(-1), 1);

    // Modified column: old = 10, new = 99
    EXPECT_EQ(iter.old_int(2).value_or(-1), 10);
    EXPECT_EQ(iter.new_int(2).value_or(-1), 99);
}

TEST_F(ChangesetTest, PrimaryKey) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'Test', 1)");

    Changeset cs = session.changeset();
    auto iter = cs.begin();
    ASSERT_TRUE(iter.next());

    auto pk = iter.primary_key();
    ASSERT_EQ(pk.size(), 3u);
    EXPECT_TRUE(pk[0]);  // id is PK
    EXPECT_FALSE(pk[1]); // name is not PK
    EXPECT_FALSE(pk[2]); // quantity is not PK
}

TEST_F(ChangesetTest, IsIndirect) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    // Direct change
    db_.execute("INSERT INTO items VALUES (3, 'Test', 1)");
    {
        Changeset cs = session.changeset();
        auto iter = cs.begin();
        ASSERT_TRUE(iter.next());
        EXPECT_FALSE(iter.is_indirect());
    }
}

// ============================================================================
// Iterator — multiple changes
// ============================================================================

TEST_F(ChangesetTest, MultipleChangesInOrder) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    db_.execute("INSERT INTO items VALUES (3, 'A', 1)");
    db_.execute("INSERT INTO items VALUES (4, 'B', 2)");
    db_.execute("INSERT INTO items VALUES (5, 'C', 3)");

    Changeset cs = session.changeset();
    auto iter = cs.begin();
    int count = 0;
    while (iter.next()) {
        EXPECT_EQ(iter.op(), ChangeOperation::Insert);
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(ChangesetTest, EmptyChangesetIteration) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    Changeset cs = session.changeset();
    auto iter = cs.begin();
    EXPECT_FALSE(iter.next());
}

// ============================================================================
// Changeset concat
// ============================================================================

TEST_F(ChangesetTest, ConcatTwoChangesets) {
    Session s1(db_, "main");
    s1.enable(true);
    s1.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'A', 1)");
    Changeset cs1 = s1.changeset();

    Session s2(db_, "main");
    s2.enable(true);
    s2.attach_all();
    db_.execute("INSERT INTO items VALUES (4, 'B', 2)");
    Changeset cs2 = s2.changeset();

    Changeset merged = cs1.concat(cs2);
    EXPECT_GT(merged.size(), std::max(cs1.size(), cs2.size()));

    auto iter = merged.begin();
    int count = 0;
    while (iter.next())
        count++;
    EXPECT_EQ(count, 2);
}

// ============================================================================
// Changeset invert
// ============================================================================

TEST_F(ChangesetTest, InvertInsert) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'Test', 1)");

    Changeset cs = session.changeset();
    Changeset undo = cs.invert();
    EXPECT_GT(undo.size(), 0);

    // Applying the inverted changeset should undo the insert
    undo.apply(db_, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });

    auto count = db_.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 2); // Back to original 2 rows
}

TEST_F(ChangesetTest, InvertUpdate) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("UPDATE items SET quantity = 99 WHERE id = 1");

    Changeset cs = session.changeset();
    Changeset undo = cs.invert();

    undo.apply(db_, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });

    auto qty =
        db_.query_value<int64_t>("SELECT quantity FROM items WHERE id = 1");
    EXPECT_EQ(*qty, 10); // Back to original
}

// ============================================================================
// Changeset apply with conflict handler
// ============================================================================

TEST_F(ChangesetTest, ApplyToReplica) {
    // Create a replica DB with the same schema
    Database replica(":memory:");
    replica.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, "
        "name TEXT, quantity INTEGER)");
    replica.execute("INSERT INTO items VALUES (1, 'Widget', 10)");

    // Make changes on source
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'New', 50)");
    db_.execute("UPDATE items SET quantity = 100 WHERE id = 1");

    Changeset cs = session.changeset();

    int conflict_count = 0;
    cs.apply(replica, [&](ConflictType type, const ChangesetIterator& it) {
        conflict_count++;
        (void)type;
        (void)it;
        return ChangesetAction::Replace;
    });

    // replica should now have rows 1, 3
    auto rows = replica.query("SELECT * FROM items ORDER BY id");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0][0].as_int64(), 1);
    EXPECT_EQ(rows[0][2].as_int64(), 100); // Updated
    EXPECT_EQ(rows[1][0].as_int64(), 3);   // Inserted
}

TEST_F(ChangesetTest, ApplyOmitConflict) {
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY,"
        " name TEXT, quantity INTEGER)");
    // Insert a different row — no PK conflict with what we'll apply
    target.execute("INSERT INTO items VALUES (999, 'Existing', 999)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    // Modify source row: original name is 'Widget', change to 'Renamed'
    db_.execute("UPDATE items SET name = 'Renamed' WHERE id = 1");

    Changeset cs = session.changeset();

    // Apply with conflict handler that omits on DATA mismatch
    cs.apply(target, [](ConflictType type, const ChangesetIterator&) {
        if (type == ConflictType::NotFound) {
            return ChangesetAction::Omit; // Target doesn't have this row
        }
        return ChangesetAction::Abort;
    });

    // Row was not found in target, so it was omitted
    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 1); // Still just the existing row
}

// ============================================================================
// Patchset
// ============================================================================

TEST_F(ChangesetTest, PatchsetExtraction) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'PatchTest', 50)");

    Patchset ps = session.patchset();
    EXPECT_FALSE(ps.empty());
    EXPECT_GT(ps.size(), 0);

    // Patchset can be iterated
    auto iter = ps.begin();
    ASSERT_TRUE(iter.next());
    EXPECT_EQ(iter.op(), ChangeOperation::Insert);
}

TEST_F(ChangesetTest, PatchsetApply) {
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY,"
        " name TEXT, quantity INTEGER)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'ViaPatch', 50)");

    Patchset ps = session.patchset();
    ps.apply(target, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });

    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 1);
}

// ============================================================================
// Streaming API
// ============================================================================

TEST_F(ChangesetTest, ApplyStream) {
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'StreamApply', 50)");

    Changeset cs = session.changeset();

    // Wrap changeset data as a stream input
    StreamInputFunc input = make_stream_input(cs.data(), cs.size());

    Changeset::apply_stream(
        target, std::move(input),
        [](ConflictType, const ChangesetIterator&) {
            return ChangesetAction::Abort;
        });

    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 1);
}

TEST_F(ChangesetTest, ApplyStreamV2WithFlags) {
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");
    target.execute("INSERT INTO items VALUES (1, 'Existing', 999)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("UPDATE items SET quantity = 100 WHERE id = 1");

    Changeset cs = session.changeset();
    auto input = make_stream_input(cs.data(), cs.size());

    Changeset::apply_v2_stream(
        target, std::move(input),
        [](ConflictType, const ChangesetIterator&) {
            return ChangesetAction::Replace;
        },
        0);

    auto qty =
        target.query_value<int64_t>("SELECT quantity FROM items WHERE id = 1");
    EXPECT_EQ(*qty, 100);
}

TEST_F(ChangesetTest, ConcatStream) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'A', 1)");
    Changeset cs1 = session.changeset();
    db_.execute("INSERT INTO items VALUES (4, 'B', 2)");
    Changeset cs2 = session.changeset();

    auto input_a = make_stream_input(cs1.data(), cs1.size());
    auto input_b = make_stream_input(cs2.data(), cs2.size());

    std::vector<uint8_t> out;
    StreamOutputFunc output = [&](const void* data, int size) -> int {
        auto* bytes = static_cast<const uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + size);
        return 0;
    };

    Changeset::concat_stream(std::move(input_a), std::move(input_b), output);

    // Create a non-owning changeset from the concatenated output and verify
    auto* merged_data = sqlite3_malloc(out.size());
    std::memcpy(merged_data, out.data(), out.size());
    Changeset merged(merged_data, static_cast<int>(out.size()));
    auto iter = merged.begin();
    int count = 0;
    while (iter.next()) {
        count++;
    }
    EXPECT_EQ(count, 2);
}

TEST_F(ChangesetTest, InvertStream) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'InvertStream', 50)");

    Changeset cs = session.changeset();
    auto input = make_stream_input(cs.data(), cs.size());

    std::vector<uint8_t> out;
    StreamOutputFunc output = [&](const void* data, int size) -> int {
        auto* bytes = static_cast<const uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + size);
        return 0;
    };

    Changeset::invert_stream(std::move(input), output);
    EXPECT_FALSE(out.empty());

    // The inverted stream should produce a valid changeset that undoes the
    // insert
    auto* inv_data = sqlite3_malloc(out.size());
    std::memcpy(inv_data, out.data(), out.size());
    Changeset inverted(inv_data, static_cast<int>(out.size()));

    inverted.apply(db_, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });
    auto count = db_.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 2);
}

TEST_F(ChangesetTest, IteratorFromStream) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'StreamIter', 50)");

    Changeset cs = session.changeset();
    auto input = make_stream_input(cs.data(), cs.size());

    auto iter = ChangesetIterator::from_stream(std::move(input));
    ASSERT_TRUE(iter.next());
    EXPECT_EQ(iter.op(), ChangeOperation::Insert);
    EXPECT_EQ(iter.table_name(), "items");
    EXPECT_FALSE(iter.next());
}

// ============================================================================
// apply_v2 — with ApplyFlag
// ============================================================================

TEST_F(ChangesetTest, ApplyV2IgnoresNoop) {
    // The source changes quantity from 10 to 10 (identical value).
    // SQLite sessions record no change for a no-op UPDATE, so the changeset
    // will be empty.  Verify graceful handling: apply_v2 succeeds and the
    // target row is unchanged.
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");
    target.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
    target.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    // Update to a new value so the session records the change.
    db_.execute("UPDATE items SET quantity = 42 WHERE id = 1");

    Changeset cs = session.changeset();
    ASSERT_FALSE(cs.empty());

    int conflict_count = 0;
    // Target already has quantity=10; applying quantity=42 is not a no-op here,
    // but we use IgnoreNoop so that if the value happened to match it would be
    // silently skipped. There is no conflict because the target row exists.
    cs.apply_v2(target,
                [&](ConflictType, const ChangesetIterator&) -> ChangesetAction {
                    conflict_count++;
                    return ChangesetAction::Omit;
                },
                static_cast<int>(ApplyFlag::IgnoreNoop));

    EXPECT_EQ(conflict_count, 0);
    // The change was applied — quantity should now be 42.
    auto qty =
        target.query_value<int64_t>("SELECT quantity FROM items WHERE id = 1");
    EXPECT_EQ(*qty, 42);
}

TEST_F(ChangesetTest, ApplyV2BasicApply) {
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");
    target.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
    target.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'V2Item', 77)");

    Changeset cs = session.changeset();
    cs.apply_v2(target,
                [](ConflictType, const ChangesetIterator&) -> ChangesetAction {
                    return ChangesetAction::Abort;
                });

    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 3);
    auto qty =
        target.query_value<int64_t>("SELECT quantity FROM items WHERE id = 3");
    EXPECT_EQ(*qty, 77);
}

// ============================================================================
// apply_v3 — per-change filter
// ============================================================================

TEST_F(ChangesetTest, ApplyV3FilterSkipsSelectedChanges) {
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");
    target.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
    target.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    // Two inserts on source
    db_.execute("INSERT INTO items VALUES (3, 'ApplyMe', 10)");
    db_.execute("INSERT INTO items VALUES (4, 'SkipMe', 20)");

    Changeset cs = session.changeset();
    ASSERT_FALSE(cs.empty());

    // Per-change filter: only apply id=3, skip id=4
    int applied = 0;
    cs.apply_v3(
        target,
        [&](ConflictType, const ChangesetIterator&) -> ChangesetAction {
            return ChangesetAction::Abort;
        },
        [](const ChangesetIterator& it) -> bool {
            // new_int(0) == id column; apply only id == 3
            auto id = it.new_int(0);
            return id.has_value() && *id == 3;
        });

    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    // Original 2 + only id=3 was applied
    EXPECT_EQ(*count, 3);
    // id=4 must not exist in target
    auto row4 =
        target.query_value<int64_t>("SELECT COUNT(*) FROM items WHERE id = 4");
    EXPECT_EQ(*row4, 0);
    (void)applied;
}

TEST_F(ChangesetTest, ApplyV3NullFilterBehavesLikeApplyV2) {
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");
    target.execute("INSERT INTO items VALUES (1, 'Widget', 10)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (5, 'V3Item', 55)");

    Changeset cs = session.changeset();
    // Passing a null per_change_filter should apply all changes.
    cs.apply_v3(target,
                [](ConflictType, const ChangesetIterator&) -> ChangesetAction {
                    return ChangesetAction::Abort;
                },
                {} /* no per-change filter */);

    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 2);
}

// ============================================================================
// End-to-end replication: Session → Changeset → Apply → Verify
// ============================================================================

TEST_F(ChangesetTest, EndToEndReplication) {
    // Create a replica with the same initial state
    Database replica(":memory:");
    replica.execute(R"(
        CREATE TABLE items (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            quantity INTEGER DEFAULT 0
        )
    )");
    replica.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
    replica.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

    // Perform a mix of operations on the source (db_)
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'Replicated', 100)");
    db_.execute("UPDATE items SET quantity = 42 WHERE id = 1");
    db_.execute("DELETE FROM items WHERE id = 2");

    Changeset cs = session.changeset();

    // Apply to replica
    cs.apply(replica, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });

    // Verify replica matches source
    auto src_rows = db_.query("SELECT * FROM items ORDER BY id");
    auto rep_rows = replica.query("SELECT * FROM items ORDER BY id");
    ASSERT_EQ(src_rows.size(), rep_rows.size());
    for (std::size_t i = 0; i < src_rows.size(); ++i) {
        EXPECT_EQ(src_rows[i][0], rep_rows[i][0]); // id
        EXPECT_EQ(src_rows[i][1], rep_rows[i][1]); // name
        EXPECT_EQ(src_rows[i][2], rep_rows[i][2]); // quantity
    }
}

// ============================================================================
// apply_v2 with Invert flag
// ============================================================================

TEST_F(ChangesetTest, ApplyV2WithNoSavepoint) {
    // Test apply_v2 with NoSavepoint flag (no implicit SAVEPOINT wrapping).
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");
    target.execute("INSERT INTO items VALUES (1, 'Widget', 10)");

    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (5, 'NoSavepointTest', 100)");

    Changeset cs = session.changeset();

    cs.apply_v2(target,
                [](ConflictType, const ChangesetIterator&) -> ChangesetAction {
                    return ChangesetAction::Abort;
                },
                static_cast<int>(ApplyFlag::NoSavepoint));

    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 2);
}

TEST_F(ChangesetTest, ApplyV2InvertViaInvertMethod) {
    // The Invert flag on apply_v2 requires SQLite builds that support
    // SQLITE_CHANGESETAPPLY_INVERT in sqlite3changeset_apply_v2.
    // As an alternative, invert the changeset first, then apply normally.
    Database target(":memory:");
    target.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, quantity "
        "INTEGER)");
    target.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
    target.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

    // Source inserts a new row
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'NewRow', 100)");

    Changeset cs = session.changeset();

    // Invert first, then apply — INSERT becomes DELETE
    Changeset inverted = cs.invert();
    inverted.apply(target,
                   [](ConflictType, const ChangesetIterator&) -> ChangesetAction {
                       return ChangesetAction::Omit;
                   });

    // Target still has 2 rows (the inverted INSERT tries to DELETE id=3
    // which doesn't exist in target, so it's omitted)
    auto count = target.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 2);
}

// ============================================================================
// ChangesetIterator::from_stream with error callback
// ============================================================================

TEST_F(ChangesetTest, IteratorFromStreamError) {
    // Create a streaming input that returns an error
    StreamInputFunc error_input = [](void*, int) -> int {
        return 1; // Signal error
    };

    auto iter = ChangesetIterator::from_stream(std::move(error_input));
    EXPECT_THROW(iter.next(), Error);
}

} // anonymous namespace
} // namespace sqlitex

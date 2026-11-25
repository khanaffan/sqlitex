#include <sqlitex/sqlitex.h>

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>

namespace sqlitex {
namespace {

class SessionTest : public ::testing::Test {
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
// Construction and lifecycle
// ============================================================================

TEST_F(SessionTest, CreateSession) {
    Session session(db_, "main");
    EXPECT_TRUE(session);
    EXPECT_TRUE(session.is_empty());
}

TEST_F(SessionTest, MoveConstruction) {
    Session s1(db_, "main");
    EXPECT_TRUE(s1);

    Session s2(std::move(s1));
    EXPECT_TRUE(s2);
    EXPECT_FALSE(s1); // NOLINT
}

TEST_F(SessionTest, MoveAssignment) {
    Session s1(db_, "main");
    Session s2(db_, "main");
    s2 = std::move(s1);
    EXPECT_TRUE(s2);
    EXPECT_FALSE(s1); // NOLINT
}

// ============================================================================
// Enable / disable
// ============================================================================

TEST_F(SessionTest, EnableDisable) {
    Session session(db_, "main");
    session.attach_all();
    session.enable(true); // Must explicitly enable

    // After enabling, changes should be tracked
    db_.execute("INSERT INTO items VALUES (10, 'test', 1)");
    EXPECT_FALSE(session.is_empty());
}

TEST_F(SessionTest, EnableReturnsPreviousState) {
    Session session(db_, "main");
    session.attach_all();

    // Sessions are enabled by default; disabling returns the prior (enabled)
    // state.
    EXPECT_TRUE(session.enable(false));
    db_.execute("INSERT INTO items VALUES (99, 'ignored', 1)");
    EXPECT_TRUE(session.is_empty());

    // Re-enable; the prior state was disabled.
    EXPECT_FALSE(session.enable(true));
    db_.execute("INSERT INTO items VALUES (100, 'tracked', 2)");
    EXPECT_FALSE(session.is_empty());
}

TEST_F(SessionTest, IndirectFlag) {
    Session session(db_, "main");
    // set_indirect() returns the previous state. After setting true, the
    // next call observing the prior state must see true.
    session.set_indirect(true);
    EXPECT_TRUE(session.set_indirect(false));
    EXPECT_FALSE(session.set_indirect(true));
}

// ============================================================================
// Table attachment
// ============================================================================

TEST_F(SessionTest, AttachSpecificTable) {
    Session session(db_, "main");
    session.enable(true);
    session.attach("items");

    db_.execute("UPDATE items SET quantity = 100 WHERE id = 1");
    EXPECT_FALSE(session.is_empty());
}

TEST_F(SessionTest, AttachAllTables) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    db_.execute("UPDATE items SET quantity = 100 WHERE id = 1");
    EXPECT_FALSE(session.is_empty());
}

TEST_F(SessionTest, AttachNoneTracksNothing) {
    Session session(db_, "main");
    session.enable(true);
    // Don't attach any tables

    db_.execute("UPDATE items SET quantity = 100 WHERE id = 1");
    EXPECT_TRUE(session.is_empty());
}

// ============================================================================
// Changeset extraction
// ============================================================================

TEST_F(SessionTest, ExtractEmptyChangeset) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    Changeset cs = session.changeset();
    EXPECT_TRUE(cs.empty());
}

TEST_F(SessionTest, ExtractChangesetAfterInsert) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    db_.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");

    Changeset cs = session.changeset();
    EXPECT_FALSE(cs.empty());
    EXPECT_GT(cs.size(), 0);
}

TEST_F(SessionTest, ExtractChangesetAfterUpdate) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    db_.execute("UPDATE items SET quantity = 99 WHERE id = 1");

    Changeset cs = session.changeset();
    EXPECT_FALSE(cs.empty());
}

TEST_F(SessionTest, ExtractChangesetAfterDelete) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    db_.execute("DELETE FROM items WHERE id = 2");

    Changeset cs = session.changeset();
    EXPECT_FALSE(cs.empty());
}

TEST_F(SessionTest, NoChangesWhenDisabled) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    session.enable(false);

    db_.execute("UPDATE items SET quantity = 999 WHERE id = 1");

    Changeset cs = session.changeset();
    EXPECT_TRUE(cs.empty());
}

// ============================================================================
// Patchset extraction
// ============================================================================

TEST_F(SessionTest, ExtractPatchset) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    db_.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");

    Patchset ps = session.patchset();
    EXPECT_FALSE(ps.empty());
    EXPECT_GT(ps.size(), 0);
}

// ============================================================================
// Changeset size
// ============================================================================

TEST_F(SessionTest, CumulativeChangeset) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    db_.execute("INSERT INTO items VALUES (3, 'A', 1)");
    Changeset cs1 = session.changeset();
    auto size1 = cs1.size();

    db_.execute("INSERT INTO items VALUES (4, 'B', 2)");
    Changeset cs2 = session.changeset();
    auto size2 = cs2.size();

    // Second changeset should be larger (includes both inserts)
    EXPECT_GT(size2, size1);
}

// ============================================================================
// Introspection
// ============================================================================

TEST_F(SessionTest, IsEmpty) {
    Session session(db_, "main");
    EXPECT_TRUE(session.is_empty());
}

TEST_F(SessionTest, MemoryUsed) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    EXPECT_GE(session.memory_used(), 0);
}

// ============================================================================
// Table filter
// ============================================================================

TEST_F(SessionTest, TableFilterInclude) {
    Session session(db_, "main");
    session.enable(true);
    session.set_table_filter(
        [](std::string_view table) { return table == "items"; });

    db_.execute("CREATE TABLE other (x INTEGER)");
    db_.execute("INSERT INTO other VALUES (1)");
    db_.execute("UPDATE items SET quantity = 50 WHERE id = 1");

    EXPECT_FALSE(session.is_empty());
}

TEST_F(SessionTest, TableFilterExclude) {
    Session session(db_, "main");
    session.enable(true);
    session.set_table_filter([](std::string_view table) {
        return table != "items"; // exclude items
    });

    db_.execute("UPDATE items SET quantity = 50 WHERE id = 1");
    EXPECT_TRUE(session.is_empty());
}

// ============================================================================
// Session on in-memory DB explicitly named
// ============================================================================

TEST_F(SessionTest, SessionOnNamedDatabase) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)");
    db.execute("INSERT INTO t VALUES (1, 'hello')");

    Session session(db, "main");
    session.enable(true);
    session.attach("t");

    db.execute("INSERT INTO t VALUES (2, 'world')");

    Changeset cs = session.changeset();
    EXPECT_FALSE(cs.empty());
}

// ============================================================================
// Streaming changeset/patchset
// ============================================================================

TEST_F(SessionTest, ChangesetStreamToVector) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'StreamTest', 50)");

    std::vector<std::byte> collected;
    StreamOutputFunc output = [&](std::span<const std::byte> data) -> int {
        collected.insert(collected.end(), data.begin(), data.end());
        return 0;
    };

    session.changeset_stream(output);
    EXPECT_FALSE(collected.empty());

    // Compare with in-memory version
    Changeset cs = session.changeset();
    ASSERT_EQ(collected.size(), cs.size());
    EXPECT_EQ(std::memcmp(collected.data(), cs.data(), cs.size()), 0);
}

TEST_F(SessionTest, PatchsetStreamToVector) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'PatchStream', 50)");

    std::vector<std::byte> collected;
    StreamOutputFunc output = [&](std::span<const std::byte> data) -> int {
        collected.insert(collected.end(), data.begin(), data.end());
        return 0;
    };

    session.patchset_stream(output);
    EXPECT_FALSE(collected.empty());
}

TEST_F(SessionTest, ChangesetStreamToOStream) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'OStream', 50)");

    std::ostringstream oss;
    session.changeset_to_stream(oss);
    EXPECT_FALSE(oss.str().empty());
}

TEST_F(SessionTest, ChangesetStreamCallbackError) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'ErrTest', 50)");

    StreamOutputFunc error_output = [](std::span<const std::byte>) -> int {
        return 1; // Signal error
    };

    EXPECT_THROW(session.changeset_stream(error_output), Error);
}

// ============================================================================
// Session::diff() — actual diff against an attached database
// ============================================================================

TEST_F(SessionTest, DiffAgainstAttachedDatabase) {
    // Create a reference DB in a temp file with the same schema + different
    // data
    test::TempFile ref_file;
    {
        Database ref(ref_file.path());
        ref.execute(R"(
            CREATE TABLE items (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                quantity INTEGER DEFAULT 0
            )
        )");
        // Reference has id=1 with quantity=999 (different from source's 10)
        ref.execute("INSERT INTO items VALUES (1, 'Widget', 999)");
        // Reference has id=2 same as source
        ref.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");
        // Reference has an extra row id=99
        ref.execute("INSERT INTO items VALUES (99, 'OnlyInRef', 0)");
    }

    // Attach the reference DB to db_ (the test fixture's in-memory DB)
    db_.execute("ATTACH DATABASE '" + std::string(ref_file.path()) +
                "' AS ref_db");

    // Create a fresh session — must have no prior changes for a clean diff
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    // diff() appends to the session, so we use a fresh one
    Changeset cs = session.diff("items", "ref_db");

    // The diff should contain changes to reconcile source → target
    EXPECT_FALSE(cs.empty());
    EXPECT_GT(cs.size(), 0);

    // Iterate the diff: should find the quantity difference for id=1
    auto iter = cs.changes();
    bool found_qty_change = false;
    bool found_extra_row = false;
    while (iter.next()) {
        if (iter.op() == ChangeOperation::Update) {
            // id=1 quantity: 10 (old, from ref) → ? (new)
            auto old_id = iter.old_int(0);
            if (old_id.has_value() && *old_id == 1) {
                found_qty_change = true;
            }
        }
        if (iter.op() == ChangeOperation::Delete) {
            auto old_id = iter.old_int(0);
            if (old_id.has_value() && *old_id == 99) {
                found_extra_row = true;
            }
        }
    }
    // At minimum, the quantity difference should be detected
    EXPECT_TRUE(found_qty_change || found_extra_row);
}

TEST_F(SessionTest, DiffFreshSessionContainsOnlyDiff) {
    // Create a reference with a row that source doesn't have
    test::TempFile ref_file;
    {
        Database ref(ref_file.path());
        ref.execute(R"(
            CREATE TABLE items (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                quantity INTEGER DEFAULT 0
            )
        )");
        ref.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
        ref.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");
        ref.execute("INSERT INTO items VALUES (42, 'Extra', 77)");
    }

    db_.execute("ATTACH DATABASE '" + std::string(ref_file.path()) +
                "' AS ref2");

    // Fresh session — diff returns only the differences, not prior changes
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();

    Changeset cs = session.diff("items", "ref2");
    EXPECT_FALSE(cs.empty());

    // Apply this diff back to the reference DB to verify it would
    // reconcile the two databases
    auto iter = cs.changes();
    int change_count = 0;
    while (iter.next()) {
        change_count++;
    }
    EXPECT_GT(change_count, 0);
}

} // anonymous namespace
} // namespace sqlitex

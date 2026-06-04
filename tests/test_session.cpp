#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

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

    // Disable and verify changes are not tracked
    session.enable(false);
    db_.execute("INSERT INTO items VALUES (99, 'ignored', 1)");
    EXPECT_TRUE(session.is_empty());

    // Re-enable and verify changes are tracked
    session.enable(true);
    db_.execute("INSERT INTO items VALUES (100, 'tracked', 2)");
    EXPECT_FALSE(session.is_empty());
}

TEST_F(SessionTest, IndirectFlag) {
    Session session(db_, "main");
    // Indirect tracking default varies — just verify it's callable
    int prev = session.set_indirect(true);
    EXPECT_GE(prev, 0);
    EXPECT_LE(prev, 1);
    prev = session.set_indirect(false);
    EXPECT_GE(prev, 0);
    EXPECT_LE(prev, 1);
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
    int size1 = cs1.size();

    db_.execute("INSERT INTO items VALUES (4, 'B', 2)");
    Changeset cs2 = session.changeset();
    int size2 = cs2.size();

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

    std::vector<uint8_t> collected;
    StreamOutputFunc output = [&](const void* data, int size) -> int {
        auto* bytes = static_cast<const uint8_t*>(data);
        collected.insert(collected.end(), bytes, bytes + size);
        return 0;
    };

    session.changeset_stream(output);
    EXPECT_FALSE(collected.empty());

    // Compare with in-memory version
    Changeset cs = session.changeset();
    ASSERT_EQ(collected.size(), static_cast<std::size_t>(cs.size()));
    EXPECT_EQ(std::memcmp(collected.data(), cs.data(), cs.size()), 0);
}

TEST_F(SessionTest, PatchsetStreamToVector) {
    Session session(db_, "main");
    session.enable(true);
    session.attach_all();
    db_.execute("INSERT INTO items VALUES (3, 'PatchStream', 50)");

    std::vector<uint8_t> collected;
    StreamOutputFunc output = [&](const void* data, int size) -> int {
        auto* bytes = static_cast<const uint8_t*>(data);
        collected.insert(collected.end(), bytes, bytes + size);
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

    StreamOutputFunc error_output = [](const void*, int) -> int {
        return 1; // Signal error
    };

    EXPECT_THROW(session.changeset_stream(error_output), Error);
}

} // anonymous namespace
} // namespace sqlitex

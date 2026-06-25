#include <sqlitex/sqlitex.h>

// Uses the raw SQLite C API directly (sqlite3_malloc), so it includes
// <sqlite3.h> explicitly.
#include <sqlite3.h>

#include <gtest/gtest.h>

namespace sqlitex {
namespace {

class ChangegroupTest : public ::testing::Test {
    protected:
    void SetUp() override {
        db1_.execute(R"(
            CREATE TABLE items (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                quantity INTEGER DEFAULT 0
            )
        )");
        db1_.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
        db1_.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

        db2_.execute(R"(
            CREATE TABLE items (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                quantity INTEGER DEFAULT 0
            )
        )");
        db2_.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
        db2_.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

        target_.execute(R"(
            CREATE TABLE items (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                quantity INTEGER DEFAULT 0
            )
        )");
    }

    Database db1_{":memory:"};
    Database db2_{":memory:"};
    Database target_{":memory:"};
};

// ============================================================================
// Construction
// ============================================================================

TEST_F(ChangegroupTest, CreateChangegroup) {
    Changegroup group;
    EXPECT_TRUE(group);
}

TEST_F(ChangegroupTest, MoveConstruction) {
    Changegroup g1;
    Changegroup g2(std::move(g1));
    EXPECT_TRUE(g2);
    EXPECT_FALSE(g1); // NOLINT
}

TEST_F(ChangegroupTest, MoveAssignment) {
    Changegroup g1;
    Changegroup g2;
    g2 = std::move(g1);
    EXPECT_TRUE(g2);
}

// ============================================================================
// Add changesets
// ============================================================================

TEST_F(ChangegroupTest, AddSingleChangeset) {
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'A', 1)");

    Changegroup group;
    group.add(s1.changeset());

    Changeset merged = group.output();
    EXPECT_FALSE(merged.empty());
}

TEST_F(ChangegroupTest, AddTwoChangesets) {
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'FromDB1', 1)");

    Session s2(db2_, "main");
    s2.enable(true);
    s2.attach_all();
    db2_.execute("INSERT INTO items VALUES (4, 'FromDB2', 2)");

    Changegroup group;
    group.add(s1.changeset());
    group.add(s2.changeset());

    Changeset merged = group.output();
    EXPECT_FALSE(merged.empty());

    auto iter = merged.begin();
    int count = 0;
    while (iter.next())
        count++;
    EXPECT_EQ(count, 2);
}

TEST_F(ChangegroupTest, AddThreeChangesets) {
    // Three independent changes
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (10, 'Ten', 10)");

    Session s2(db1_, "main");
    s2.enable(true);
    s2.attach_all();
    db1_.execute("INSERT INTO items VALUES (20, 'Twenty', 20)");

    Session s3(db1_, "main");
    s3.enable(true);
    s3.attach_all();
    db1_.execute("INSERT INTO items VALUES (30, 'Thirty', 30)");

    Changegroup group;
    group.add(s1.changeset());
    group.add(s2.changeset());
    group.add(s3.changeset());

    Changeset merged = group.output();

    auto iter = merged.begin();
    int count = 0;
    while (iter.next())
        count++;
    EXPECT_EQ(count, 3);
}

// ============================================================================
// Add patchsets
// ============================================================================

TEST_F(ChangegroupTest, AddPatchset) {
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'ViaPatch', 50)");

    Changegroup group;
    group.add(s1.patchset());

    Changeset merged = group.output();
    EXPECT_FALSE(merged.empty());
}

TEST_F(ChangegroupTest, MixChangesetsAndPatchsets) {
    // Both as changesets (SQLite changegroups don't support mixing types)
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'CS', 1)");

    Session s2(db2_, "main");
    s2.enable(true);
    s2.attach_all();
    db2_.execute("INSERT INTO items VALUES (4, 'PS', 2)");

    Changegroup group;
    group.add(s1.changeset());
    group.add(s2.changeset());

    Changeset merged = group.output();

    auto iter = merged.begin();
    int count = 0;
    while (iter.next())
        count++;
    EXPECT_EQ(count, 2);
}

// ============================================================================
// Apply merged changesets
// ============================================================================

TEST_F(ChangegroupTest, ApplyMergedToTarget) {
    // Seed target with initial data to avoid NOTFOUND on UPDATE
    target_.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
    target_.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'Alice', 100)");
    db1_.execute("UPDATE items SET quantity = 200 WHERE id = 1");

    Session s2(db2_, "main");
    s2.enable(true);
    s2.attach_all();
    db2_.execute("INSERT INTO items VALUES (4, 'Bob', 300)");

    Changegroup group;
    group.add(s1.changeset());
    group.add(s2.changeset());

    Changeset merged = group.output();
    merged.apply(target_, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Replace;
    });

    auto rows = target_.query("SELECT * FROM items ORDER BY id");
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0][0].as_int64(), 1);
    EXPECT_EQ(rows[0][2].as_int64(), 200); // Updated
    EXPECT_EQ(rows[1][0].as_int64(), 2);
    EXPECT_EQ(rows[1][2].as_int64(), 5); // Unchanged
    EXPECT_EQ(rows[2][0].as_int64(), 3); // From db1
    EXPECT_EQ(rows[3][0].as_int64(), 4); // From db2
}

// ============================================================================
// Empty output
// ============================================================================

TEST_F(ChangegroupTest, EmptyGroupProducesEmptyChangeset) {
    Changegroup group;
    Changeset merged = group.output();
    EXPECT_TRUE(merged.empty());
}

// ============================================================================
// Concat multiple outputs
// ============================================================================

TEST_F(ChangegroupTest, OutputMultipleTimes) {
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'First', 1)");

    Changegroup group;
    group.add(s1.changeset());

    Changeset out1 = group.output();
    EXPECT_FALSE(out1.empty());

    // Output again produces the same merged result
    Changeset out2 = group.output();
    EXPECT_EQ(out1.size(), out2.size());
}

// ============================================================================
// Streaming changegroup API
// ============================================================================

TEST_F(ChangegroupTest, AddStream) {
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'StreamAdd', 100)");

    Changeset cs = s1.changeset();
    auto input = make_stream_input(cs.data(), cs.size());

    Changegroup group;
    group.add_stream(std::move(input));

    Changeset merged = group.output();
    EXPECT_FALSE(merged.empty());
}

TEST_F(ChangegroupTest, OutputStream) {
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'StreamOut', 100)");

    Changegroup group;
    group.add(s1.changeset());

    std::vector<uint8_t> out;
    StreamOutputFunc output = [&](const void* data, int size) -> int {
        auto* bytes = static_cast<const uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + size);
        return 0;
    };

    group.output_stream(output);
    EXPECT_FALSE(out.empty());

    // Compare with in-memory output
    Changeset mem = group.output();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(mem.size()));
    EXPECT_EQ(std::memcmp(out.data(), mem.data(), mem.size()), 0);
}

TEST_F(ChangegroupTest, AddAndOutputStreamRoundTrip) {
    Session s1(db1_, "main");
    s1.enable(true);
    s1.attach_all();
    db1_.execute("INSERT INTO items VALUES (3, 'RoundTrip', 100)");

    Changeset cs = s1.changeset();
    auto input = make_stream_input(cs.data(), cs.size());

    Changegroup group;
    group.add_stream(std::move(input));

    std::vector<uint8_t> out;
    StreamOutputFunc output = [&](const void* data, int size) -> int {
        auto* bytes = static_cast<const uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + size);
        return 0;
    };
    group.output_stream(output);

    EXPECT_FALSE(out.empty());

    // Verify the output can be applied to a target database
    auto* merged_data = sqlite3_malloc(out.size());
    std::memcpy(merged_data, out.data(), out.size());
    Changeset merged(merged_data, static_cast<int>(out.size()));

    merged.apply(target_, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });

    auto count =
        target_.query_value<int64_t>("SELECT COUNT(*) FROM items");
    EXPECT_EQ(*count, 1);
}

} // anonymous namespace
} // namespace sqlitex

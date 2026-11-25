#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {
namespace {

class BlobIOTest : public ::testing::Test {
    protected:
    void SetUp() override {
        db_.execute(
            "CREATE TABLE docs ("
            "  id INTEGER PRIMARY KEY,"
            "  name TEXT,"
            "  content BLOB"
            ")");
        // Row 1: 256-byte binary blob
        std::vector<uint8_t> buf256(256);
        for (int i = 0; i < 256; ++i)
            buf256[i] = static_cast<uint8_t>(i);
        auto stmt = db_.prepare(
            "INSERT INTO docs (id, name, content) VALUES (?, ?, ?)");
        stmt.bind(1, static_cast<int64_t>(1))
            .bind(2, std::string_view("binary_data"))
            .bind(3, buf256);
        stmt.execute();

        // Row 2: text content
        auto stmt2 = db_.prepare(
            "INSERT INTO docs (id, name, content) VALUES (?, ?, ?)");
        stmt2.bind(1, static_cast<int64_t>(2))
            .bind(2, std::string_view("hello_world"))
            .bind(3, std::string_view("Hello, World!"));
        stmt2.execute();

        // Row 3: NULL blob
        db_.execute(
            "INSERT INTO docs (id, name, content) VALUES (3, 'null_doc', "
            "NULL)");
    }

    Database db_{":memory:"};
};

// ============================================================================
// Construction
// ============================================================================

TEST_F(BlobIOTest, OpenReadOnly) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    EXPECT_TRUE(blob);
    EXPECT_EQ(blob.size(), 256);
}

TEST_F(BlobIOTest, OpenReadWrite) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);
    EXPECT_TRUE(blob);
    EXPECT_EQ(blob.size(), 256);
}

TEST_F(BlobIOTest, OpenFailOnNonexistentRow) {
    EXPECT_THROW(BlobIO(db_, "main", "docs", "content", 999), Error);
}

TEST_F(BlobIOTest, OpenFailOnNonexistentTable) {
    EXPECT_THROW(BlobIO(db_, "main", "no_such_table", "content", 1), Error);
}

TEST_F(BlobIOTest, OpenFailOnNonexistentColumn) {
    EXPECT_THROW(BlobIO(db_, "main", "docs", "no_such_column", 1), Error);
}

TEST_F(BlobIOTest, DefaultConstructorStartsClosed) {
    BlobIO blob;
    EXPECT_FALSE(blob);
    EXPECT_EQ(blob.handle(), nullptr);
}

// ============================================================================
// Read
// ============================================================================

TEST_F(BlobIOTest, ReadEntireBlob) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    std::vector<uint8_t> buf(256);
    blob.read(buf, 0);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(buf[i], static_cast<uint8_t>(i))
            << "mismatch at offset " << i;
    }
}

TEST_F(BlobIOTest, ReadPartialBlob) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    std::vector<uint8_t> buf(4);
    // Read bytes 100..103
    blob.read(buf, 100);
    EXPECT_EQ(buf[0], 100);
    EXPECT_EQ(buf[1], 101);
    EXPECT_EQ(buf[2], 102);
    EXPECT_EQ(buf[3], 103);
}

TEST_F(BlobIOTest, ReadSingleByte) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    std::array<uint8_t, 1> byte{};
    blob.read(byte, 255);
    EXPECT_EQ(byte[0], 255);
}

TEST_F(BlobIOTest, ReadAtOffsetZero) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    std::array<uint8_t, 1> byte{};
    blob.read(byte, 0);
    EXPECT_EQ(byte[0], 0);
}

TEST_F(BlobIOTest, ReadBeyondBoundsThrows) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    std::vector<uint8_t> buf(10);
    // Blob is 256 bytes, reading 10 at offset 255 goes 9 bytes over
    EXPECT_THROW(blob.read(buf, 255), Error);
}

TEST_F(BlobIOTest, ReadTextAsBlob) {
    BlobIO blob(db_, "main", "docs", "content", 2);
    EXPECT_EQ(blob.size(), 13);
    std::string buf(static_cast<std::size_t>(blob.size()), '\0');
    blob.read(std::as_writable_bytes(std::span(buf.data(), buf.size())), 0);
    EXPECT_EQ(buf, "Hello, World!");
}

// ============================================================================
// Write
// ============================================================================

TEST_F(BlobIOTest, WriteEntireBlob) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);
    std::vector<uint8_t> new_data(256, 0xAB);
    blob.write(new_data, 0);

    // Verify via read back
    std::vector<uint8_t> verify(256);
    blob.read(verify, 0);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(verify[i], 0xAB) << "mismatch at offset " << i;
    }
}

TEST_F(BlobIOTest, WritePartialBlob) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);

    // Write 4 bytes at offset 10
    std::vector<uint8_t> marker = {0xDE, 0xAD, 0xBE, 0xEF};
    blob.write(marker, 10);

    // Read back those 4 bytes
    std::vector<uint8_t> verify(4);
    blob.read(verify, 10);
    EXPECT_EQ(verify[0], 0xDE);
    EXPECT_EQ(verify[1], 0xAD);
    EXPECT_EQ(verify[2], 0xBE);
    EXPECT_EQ(verify[3], 0xEF);

    // Other bytes unchanged
    std::array<uint8_t, 1> first{};
    blob.read(first, 0);
    EXPECT_EQ(first[0], 0);
}

TEST_F(BlobIOTest, WriteBeyondBoundsThrows) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);
    std::vector<uint8_t> buf(10, 0xFF);
    // Blob is 256 bytes, writing 10 at offset 255 goes 9 bytes over
    EXPECT_THROW(blob.write(buf, 255), Error);
}

TEST_F(BlobIOTest, WriteReadOnlyThrows) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadOnly);
    std::array<uint8_t, 1> byte{};
    EXPECT_THROW(blob.write(byte, 0), Error);
}

// ============================================================================
// Size
// ============================================================================

TEST_F(BlobIOTest, SizeOnNonNullBlob) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    EXPECT_EQ(blob.size(), 256);
}

TEST_F(BlobIOTest, SizeOnTextBlob) {
    BlobIO blob(db_, "main", "docs", "content", 2);
    EXPECT_EQ(blob.size(), 13); // strlen("Hello, World!")
}

// ============================================================================
// Reopen
// ============================================================================

TEST_F(BlobIOTest, ReopenMovesToNewRow) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    EXPECT_EQ(blob.size(), 256);

    blob.reopen(2);
    EXPECT_EQ(blob.size(), 13); // "Hello, World!"

    // Verify we can read the new row's data
    auto data = blob.read_all_text();
    EXPECT_EQ(data, "Hello, World!");
}

TEST_F(BlobIOTest, ReopenFailsOnNonexistentRow) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    EXPECT_THROW(blob.reopen(999), Error);
}

TEST_F(BlobIOTest, ReopenPreservesReadWriteMode) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);
    blob.reopen(2);
    // Should still be writable
    std::array<uint8_t, 1> new_byte{0x42};
    EXPECT_NO_THROW(blob.write(new_byte, 0));

    // Verify write persisted
    std::array<uint8_t, 1> verify{};
    blob.read(verify, 0);
    EXPECT_EQ(verify[0], 0x42);
}

// ============================================================================
// Convenience methods
// ============================================================================

TEST_F(BlobIOTest, ReadAll) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    auto data = blob.read_all();
    ASSERT_EQ(data.size(), 256u);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(data[i], static_cast<std::byte>(i)) << "at offset " << i;
    }
}

TEST_F(BlobIOTest, ReadAllEmpty) {
    // Row 4: empty blob
    db_.execute(
        "INSERT INTO docs (id, name, content) VALUES (4, 'empty', X'')");
    BlobIO blob(db_, "main", "docs", "content", 4);
    auto data = blob.read_all();
    EXPECT_TRUE(data.empty());
}

TEST_F(BlobIOTest, ReadAllText) {
    BlobIO blob(db_, "main", "docs", "content", 2);
    auto text = blob.read_all_text();
    EXPECT_EQ(text, "Hello, World!");
}

TEST_F(BlobIOTest, WriteAllVector) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);
    std::vector<uint8_t> new_data = {0xAA, 0xBB, 0xCC, 0xDD};
    blob.write_all(new_data);

    auto verify = blob.read_all();
    ASSERT_EQ(verify.size(), 256u); // size unchanged
    EXPECT_EQ(verify[0], std::byte{0xAA});
    EXPECT_EQ(verify[1], std::byte{0xBB});
    EXPECT_EQ(verify[2], std::byte{0xCC});
    EXPECT_EQ(verify[3], std::byte{0xDD});
    // Rest of blob unchanged
    EXPECT_EQ(verify[4], std::byte{4});
}

TEST_F(BlobIOTest, WriteAllText) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);
    blob.write_all(std::string_view("ABCDEFGH"));

    auto text = blob.read_all_text();
    EXPECT_EQ(text.substr(0, 8), "ABCDEFGH");
    EXPECT_EQ(text[8], '\x08'); // original byte 8 unchanged
}

// ============================================================================
// Move semantics
// ============================================================================

TEST_F(BlobIOTest, MoveConstruction) {
    BlobIO blob1(db_, "main", "docs", "content", 1);
    EXPECT_TRUE(blob1);
    EXPECT_EQ(blob1.size(), 256);

    BlobIO blob2(std::move(blob1));
    EXPECT_TRUE(blob2);
    EXPECT_FALSE(blob1); // NOLINT

    EXPECT_EQ(blob2.size(), 256);
    auto data = blob2.read_all();
    ASSERT_EQ(data.size(), 256u);
}

TEST_F(BlobIOTest, MoveAssignment) {
    BlobIO blob1(db_, "main", "docs", "content", 1);
    BlobIO blob2(db_, "main", "docs", "content", 2);

    blob2 = std::move(blob1);
    EXPECT_TRUE(blob2);
    EXPECT_FALSE(blob1); // NOLINT

    EXPECT_EQ(blob2.size(), 256); // Now points to row 1
}

TEST_F(BlobIOTest, SelfMoveAssignment) {
    BlobIO blob(db_, "main", "docs", "content", 1);
    BlobIO& ref = blob;
    blob = std::move(ref); // Self-assignment should be safe
    EXPECT_TRUE(blob);
    EXPECT_EQ(blob.size(), 256);
}

// ============================================================================
// Integration: BlobIO + Statement for blob manipulation
// ============================================================================

TEST_F(BlobIOTest, WriteViaBlobThenVerifyViaStatement) {
    BlobIO blob(db_, "main", "docs", "content", 1, BlobOpenMode::ReadWrite);

    // Write known pattern via blob I/O
    std::string_view pattern = "BlobIO test pattern";
    blob.write(pattern, 0);

    // Verify via SQL SELECT
    auto stmt = db_.prepare("SELECT content FROM docs WHERE id = 1");
    ASSERT_TRUE(stmt.step());
    auto data = stmt.column_text(0);
    EXPECT_EQ(data.substr(0, pattern.size()), pattern);
}

TEST_F(BlobIOTest, UpdateBlobSizeThenWriteViaBlobIO) {
    // Use UPDATE to shrink the blob, then verify via BlobIO
    db_.execute("UPDATE docs SET content = X'CAFE' WHERE id = 1");

    BlobIO blob(db_, "main", "docs", "content", 1);
    EXPECT_EQ(blob.size(), 2);
    auto data = blob.read_all();
    ASSERT_EQ(data.size(), 2u);
    EXPECT_EQ(data[0], std::byte{0xCA});
    EXPECT_EQ(data[1], std::byte{0xFE});
}

TEST_F(BlobIOTest, RowidFromStatementUsedInBlobIO) {
    // Insert a row and get its rowid
    auto insert = db_.prepare("INSERT INTO docs (name, content) VALUES (?, ?)");
    insert.bind(1, std::string_view("new_doc"))
        .bind(2, std::string_view("new content here"));
    insert.execute();

    int64_t rowid = db_.last_insert_rowid();

    // Now open it via BlobIO
    BlobIO blob(db_, "main", "docs", "content", rowid);
    auto text = blob.read_all_text();
    EXPECT_EQ(text, "new content here");
}

} // anonymous namespace
} // namespace sqlitex

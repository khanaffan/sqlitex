#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <stdexcept>

namespace sqlitex {
namespace {

// ============================================================================
// ErrorCode mapping
// ============================================================================

TEST(ErrorTest, MapErrorCode) {
    EXPECT_EQ(map_error_code(SQLITE_OK), ErrorCode::Ok);
    EXPECT_EQ(map_error_code(SQLITE_ERROR), ErrorCode::Error);
    EXPECT_EQ(map_error_code(SQLITE_BUSY), ErrorCode::Busy);
    EXPECT_EQ(map_error_code(SQLITE_CONSTRAINT), ErrorCode::Constraint);
    EXPECT_EQ(map_error_code(SQLITE_CORRUPT), ErrorCode::Corrupt);
    EXPECT_EQ(map_error_code(SQLITE_IOERR), ErrorCode::IoErr);
    EXPECT_EQ(map_error_code(SQLITE_CANTOPEN), ErrorCode::CantOpen);
    EXPECT_EQ(map_error_code(SQLITE_NOTADB), ErrorCode::NotADb);
    EXPECT_EQ(map_error_code(SQLITE_ROW), ErrorCode::Row);
    EXPECT_EQ(map_error_code(SQLITE_DONE), ErrorCode::Done);
}

// ============================================================================
// Error construction
// ============================================================================

TEST(ErrorTest, ConstructFromSqliteRc) {
    Error e(SQLITE_BUSY, "database is locked");
    EXPECT_EQ(e.code(), SQLITE_BUSY);
    EXPECT_EQ(e.error_code(), ErrorCode::Busy);
    EXPECT_STREQ(e.what(), "database is locked");
}

TEST(ErrorTest, ConstructFromErrorCode) {
    Error e(ErrorCode::CantOpen, "cannot open file");
    EXPECT_EQ(e.code(), static_cast<int>(ErrorCode::CantOpen));
    EXPECT_EQ(e.error_code(), ErrorCode::CantOpen);
}

TEST(ErrorTest, ExtendedErrorCode) {
    Error e(SQLITE_BUSY, "locked");
    // Extended code may differ from primary
    EXPECT_GE(e.extended_code(), 0);
}

// ============================================================================
// Exception hierarchy
// ============================================================================

TEST(ErrorTest, CatchAsBase) {
    try {
        throw BusyError(ErrorCode::Busy, "busy");
    } catch (const Error& e) {
        EXPECT_EQ(e.error_code(), ErrorCode::Busy);
    }
}

TEST(ErrorTest, CatchSpecific) {
    try {
        throw ConstraintError(ErrorCode::Constraint, "unique violation");
    } catch (const ConstraintError& e) {
        EXPECT_EQ(e.error_code(), ErrorCode::Constraint);
    } catch (...) {
        FAIL() << "Should have caught ConstraintError";
    }
}

TEST(ErrorTest, CatchAsStdException) {
    try {
        throw IoError(ErrorCode::IoErr, "io error");
    } catch (const std::exception& e) {
        SUCCEED();
    }
}

// ============================================================================
// throw_on_error
// ============================================================================

TEST(ErrorTest, ThrowOnErrorOk) {
    EXPECT_NO_THROW(throw_on_error(SQLITE_OK, (const char*)"ok"));
}

TEST(ErrorTest, ThrowOnErrorRow) {
    EXPECT_NO_THROW(throw_on_error(SQLITE_ROW, (const char*)"row"));
}

TEST(ErrorTest, ThrowOnErrorDone) {
    EXPECT_NO_THROW(throw_on_error(SQLITE_DONE, (const char*)"done"));
}

TEST(ErrorTest, ThrowOnErrorBusy) {
    EXPECT_THROW(throw_on_error(SQLITE_BUSY, (const char*)"busy"), BusyError);
}

TEST(ErrorTest, ThrowOnErrorConstraint) {
    EXPECT_THROW(throw_on_error(SQLITE_CONSTRAINT, (const char*)"constraint"),
                 ConstraintError);
}

// ============================================================================
// Changeset-specific errors
// ============================================================================

TEST(ErrorTest, ChangesetApplyError) {
    ChangesetApplyError e(ErrorCode::Error, "apply failed");
    EXPECT_EQ(e.error_code(), ErrorCode::Error);
    EXPECT_STREQ(e.what(), "apply failed");
}

} // anonymous namespace
} // namespace sqlitex

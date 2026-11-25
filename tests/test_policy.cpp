#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace sqlitex {
namespace {

using Action = AuthorizerAction;
using Result = AuthorizerResult;

static_assert(!std::is_copy_constructible_v<ScopedPolicy>);
static_assert(!std::is_copy_assignable_v<ScopedPolicy>);
static_assert(!std::is_move_constructible_v<ScopedPolicy>);
static_assert(!std::is_move_assignable_v<ScopedPolicy>);

struct Seen {
    Action action;
    std::optional<std::string> arg1, arg2, database, source;
};

std::optional<std::string> own(const std::optional<std::string_view>& v) {
    return v ? std::optional<std::string>(std::string(*v)) : std::nullopt;
}

AuthorizerCallback recorder(std::vector<Seen>& out) {
    return [&out](const AuthorizerRequest& r) {
        out.push_back({r.action, own(r.arg1), own(r.arg2), own(r.database),
                       own(r.source)});
        return Result::Allow;
    };
}

bool saw(const std::vector<Seen>& seen, Action a) {
    for (const auto& s : seen) {
        if (s.action == a)
            return true;
    }
    return false;
}

class TempFile {
    public:
    explicit TempFile(const std::string& stem) {
        static std::atomic<int> counter{0};
        path_ = (std::filesystem::temp_directory_path() /
                 ("sqlitex_policy_" + stem + "_" + std::to_string(::getpid()) +
                  "_" + std::to_string(counter++) + ".db"))
                    .string();
        remove();
    }
    ~TempFile() {
        remove();
    }
    const std::string& path() const {
        return path_;
    }

    private:
    void remove() {
        std::error_code ec;
        for (const char* suffix : {"", "-journal", "-wal", "-shm"}) {
            std::filesystem::remove(path_ + suffix, ec);
        }
    }
    std::string path_;
};

class PolicyTest : public ::testing::Test {
    protected:
    void SetUp() override {
        db_.execute(
            "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, "
            "password_hash TEXT, ssn TEXT);"
            "INSERT INTO users VALUES(1,'alice','h1','111'),"
            "(2,'bob','h2','222');"
            "CREATE TABLE audit_log(id INTEGER PRIMARY KEY, msg TEXT);"
            "INSERT INTO audit_log VALUES(1,'boot');");
    }

    int64_t count(const char* table) {
        return *db_.query_value<int64_t>(std::string("SELECT count(*) FROM ") +
                                         table);
    }

    Database db_{":memory:"};
};

// ============================================================================
// Core scope behavior
// ============================================================================

TEST_F(PolicyTest, DenyThrowsAuthErrorAndLeavesDataUnchanged) {
    {
        ScopedPolicy policy(db_);
        policy.deny_insert();
        EXPECT_THROW(db_.execute("INSERT INTO users(name) VALUES('eve')"),
                     AuthError);
        EXPECT_THROW((void)db_.prepare("INSERT INTO users(name) VALUES('e')"),
                     AuthError);
        EXPECT_EQ(db_.query("SELECT name FROM users ORDER BY id").size(), 2u);
    }
    EXPECT_EQ(count("users"), 2);
}

TEST_F(PolicyTest, ScopeExitRestoresUnrestrictedAccess) {
    {
        ScopedPolicy policy(db_);
        policy.deny_delete();
        EXPECT_THROW(db_.execute("DELETE FROM audit_log"), AuthError);
    }
    db_.execute("DELETE FROM audit_log");
    EXPECT_EQ(count("audit_log"), 0);
}

TEST_F(PolicyTest, ExceptionUnwindingRestores) {
    try {
        ScopedPolicy policy(db_);
        policy.deny_delete();
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
    }
    EXPECT_NO_THROW(db_.execute("DELETE FROM audit_log"));
}

TEST_F(PolicyTest, NestedScopeReplacesAndRestoresOuter) {
    ScopedPolicy outer(db_);
    outer.deny_delete();
    EXPECT_THROW(db_.execute("DELETE FROM audit_log WHERE id=99"), AuthError);
    {
        ScopedPolicy inner(db_);
        inner.deny_insert();
        EXPECT_NO_THROW(db_.execute("DELETE FROM audit_log WHERE id=99"));
        EXPECT_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES('x')"),
                     AuthError);
    }
    EXPECT_THROW(db_.execute("DELETE FROM audit_log WHERE id=99"), AuthError);
    EXPECT_NO_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES('x')"));
}

TEST_F(PolicyTest, SeparateConnectionsAreIndependent) {
    Database other(":memory:");
    other.execute("CREATE TABLE t(x)");
    ScopedPolicy policy(db_);
    policy.deny_insert();
    EXPECT_NO_THROW(other.execute("INSERT INTO t VALUES(1)"));
}

TEST_F(PolicyTest, CallbackConstructorAndAdditionsAfterUse) {
    ScopedPolicy policy(db_, [](const AuthorizerRequest& r) {
        return r.action == Action::Delete ? Result::Deny : Result::Allow;
    });
    EXPECT_NO_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES('x')"));
    EXPECT_THROW(db_.execute("DELETE FROM audit_log"), AuthError);
    policy.deny_insert();
    EXPECT_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES('y')"),
                 AuthError);
}

TEST_F(PolicyTest, RequestFieldsPreserveNullEmptySchemaAndSource) {
    db_.execute(
        "CREATE VIEW v_users AS SELECT name FROM users;"
        "CREATE TABLE log2(m TEXT);"
        "CREATE TRIGGER trg AFTER INSERT ON audit_log BEGIN "
        "INSERT INTO log2 VALUES(new.msg); END;");
    std::vector<Seen> seen;
    ScopedPolicy policy(db_, recorder(seen));

    (void)db_.query("SELECT count(*) FROM users");
    bool count_read = false;
    for (const auto& s : seen) {
        if (s.action == Action::Read && s.arg1 == "users" && s.arg2 == "" &&
            !s.database) {
            count_read = true;
        }
        if (s.action == Action::Select) {
            EXPECT_FALSE(s.arg1);
            EXPECT_FALSE(s.arg2);
        }
    }
    EXPECT_TRUE(count_read);

    seen.clear();
    (void)db_.query("SELECT name FROM v_users");
    bool via_view = false;
    for (const auto& s : seen) {
        if (s.action == Action::Read && s.arg1 == "users" && s.arg2 == "name" &&
            s.database == "main" && s.source == "v_users")
            via_view = true;
    }
    EXPECT_TRUE(via_view);

    seen.clear();
    db_.execute("INSERT INTO audit_log(msg) VALUES('t')");
    bool via_trigger = false;
    for (const auto& s : seen) {
        if (s.action == Action::Insert && s.arg1 == "log2" && s.source == "trg")
            via_trigger = true;
    }
    EXPECT_TRUE(via_trigger);
}

TEST_F(PolicyTest, PreparedStatementIsReauthorizedOnEntryAdditionAndRestore) {
    auto stmt = db_.prepare("SELECT name FROM users");
    {
        ScopedPolicy policy(db_);
        policy.deny_select();
        EXPECT_THROW((void)stmt.step(), AuthError);
    }
    // sqlite3_reset() reports the failed step; the statement is then usable.
    EXPECT_THROW(stmt.reset(), AuthError);
    EXPECT_TRUE(stmt.step());
    stmt.reset();

    ScopedPolicy policy(db_);
    EXPECT_TRUE(stmt.step());
    stmt.reset();
    policy.deny_read();
    EXPECT_THROW((void)stmt.step(), AuthError);
}

TEST_F(PolicyTest, EveryActionCodeIsEmitted) {
    TempFile attached("attach");
    db_.execute(
        "CREATE TABLE base(a INTEGER PRIMARY KEY, b TEXT);"
        "CREATE INDEX base_b ON base(b);"
        "CREATE VIEW v AS SELECT * FROM base;"
        "CREATE TRIGGER tg AFTER INSERT ON base BEGIN SELECT 1; END;"
        "CREATE TEMP TABLE tt(x); CREATE INDEX temp.tti ON tt(x);"
        "CREATE TEMP VIEW tv AS SELECT 1;"
        "CREATE TEMP TRIGGER ttg AFTER INSERT ON tt BEGIN SELECT 1; END;");
    struct Case {
        Action action;
        std::string sql;
    };
    const std::vector<Case> cases = {
        {Action::CreateIndex, "CREATE INDEX i2 ON base(b)"},
        {Action::CreateTable, "CREATE TABLE n(x)"},
        {Action::CreateTempIndex, "CREATE INDEX temp.ti2 ON tt(x)"},
        {Action::CreateTempTable, "CREATE TEMP TABLE tn(x)"},
        {Action::CreateTempTrigger,
         "CREATE TEMP TRIGGER t2 AFTER DELETE ON tt BEGIN SELECT 1; END"},
        {Action::CreateTempView, "CREATE TEMP VIEW tv2 AS SELECT 1"},
        {Action::CreateTrigger,
         "CREATE TRIGGER t3 AFTER DELETE ON base BEGIN SELECT 1; END"},
        {Action::CreateView, "CREATE VIEW v2 AS SELECT 1"},
        {Action::Delete, "DELETE FROM base"},
        {Action::DropIndex, "DROP INDEX base_b"},
        {Action::DropTable, "DROP TABLE base"},
        {Action::DropTempIndex, "DROP INDEX temp.tti"},
        {Action::DropTempTable, "DROP TABLE tt"},
        {Action::DropTempTrigger, "DROP TRIGGER ttg"},
        {Action::DropTempView, "DROP VIEW tv"},
        {Action::DropTrigger, "DROP TRIGGER tg"},
        {Action::DropView, "DROP VIEW v"},
        {Action::Insert, "INSERT INTO base(b) VALUES('x')"},
        {Action::Pragma, "PRAGMA user_version"},
        {Action::Read, "SELECT b FROM base"},
        {Action::Select, "SELECT 1"},
        {Action::Transaction, "BEGIN"},
        {Action::Update, "UPDATE base SET b='y'"},
        {Action::Attach, "ATTACH '" + attached.path() + "' AS aux"},
        {Action::Detach, "DETACH aux"},
        {Action::AlterTable, "ALTER TABLE base ADD COLUMN c"},
        {Action::Reindex, "REINDEX base_b"},
        {Action::Analyze, "ANALYZE base"},
        {Action::CreateVtable, "CREATE VIRTUAL TABLE ft USING fts5(x)"},
        {Action::DropVtable, "DROP TABLE ft"},
        {Action::Function, "SELECT abs(-1)"},
        {Action::Savepoint, "SAVEPOINT sp"},
        {Action::Recursive,
         "WITH RECURSIVE c(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM c "
         "WHERE n<2) SELECT n FROM c"},
    };
    for (const auto& c : cases) {
        std::vector<Seen> seen;
        {
            ScopedPolicy policy(db_, recorder(seen));
            (void)db_.prepare(c.sql);
        }
        EXPECT_TRUE(saw(seen, c.action))
            << "action " << static_cast<int>(c.action) << " for " << c.sql;
        if (c.action == Action::Detach || c.action == Action::DropVtable) {
            continue; // depend on preceding statements being executed
        }
        if (c.action == Action::Attach) {
            db_.execute(c.sql);
        } else if (c.action == Action::CreateVtable) {
            db_.execute(c.sql);
        }
    }
}

// ============================================================================
// Built-in constraints
// ============================================================================

TEST_F(PolicyTest, StrictHelpersBlockCoupledInternalRequests) {
    ScopedPolicy p1(db_);
    p1.deny_insert();
    EXPECT_THROW(db_.execute("CREATE TABLE t(x)"), AuthError);
    {
        ScopedPolicy p2(db_);
        p2.deny_reindex();
        EXPECT_THROW(db_.execute("CREATE INDEX i ON users(name)"), AuthError);
    }
    {
        ScopedPolicy p3(db_);
        p3.deny_attach();
        EXPECT_THROW(db_.execute("VACUUM"), AuthError);
    }
}

TEST_F(PolicyTest, TransactionRulesAreStartOnly) {
    for (int mode = 0; mode < 3; ++mode) {
        db_.execute("BEGIN; SAVEPOINT a; SAVEPOINT b;");
        ScopedPolicy policy(db_);
        if (mode == 0)
            policy.deny_transaction();
        if (mode == 1)
            policy.deny_savepoint();
        if (mode == 2)
            policy.allow_only({Action::Read});
        if (mode == 0) {
            EXPECT_THROW(db_.execute("BEGIN"), AuthError);
        } else if (mode == 1) {
            EXPECT_THROW(db_.execute("SAVEPOINT c"), AuthError);
        } else {
            EXPECT_THROW(db_.execute("SAVEPOINT c"), AuthError);
        }
        EXPECT_NO_THROW(db_.execute("ROLLBACK TO b"));
        EXPECT_NO_THROW(db_.execute("RELEASE b"));
        EXPECT_NO_THROW(db_.execute(mode == 1 ? "ROLLBACK" : "COMMIT"));
        EXPECT_TRUE(db_.is_autocommit());
    }
}

TEST_F(PolicyTest, ReadOnlyAllowsQueriesAndIntrospection) {
    db_.execute(
        "CREATE VIRTUAL TABLE docs USING fts5(body);"
        "INSERT INTO docs VALUES('hello world'),('other text');");
    ScopedPolicy policy(db_);
    policy.read_only();

    EXPECT_EQ(db_.query("SELECT name FROM users").size(), 2u);
    EXPECT_NO_THROW(db_.execute("BEGIN; SAVEPOINT s; RELEASE s; COMMIT;"));
    EXPECT_FALSE(db_.query("PRAGMA table_info(users)").empty());
    EXPECT_FALSE(
        db_.query("SELECT name FROM pragma_table_info('users')").empty());
    EXPECT_EQ(
        db_.query("SELECT rowid FROM docs WHERE docs MATCH 'hello'").size(),
        1u);

    EXPECT_THROW(db_.execute("INSERT INTO users(name) VALUES('x')"), AuthError);
    EXPECT_THROW(db_.execute("UPDATE users SET name='x'"), AuthError);
    EXPECT_THROW(db_.execute("DELETE FROM users"), AuthError);
    EXPECT_THROW(db_.execute("CREATE TABLE t(x)"), AuthError);
    EXPECT_THROW(db_.execute("ATTACH ':memory:' AS aux"), AuthError);
    EXPECT_THROW(db_.execute("DETACH temp"), AuthError);
    EXPECT_THROW(db_.execute("VACUUM"), AuthError);
    EXPECT_THROW(db_.execute("PRAGMA user_version=5"), AuthError);
    EXPECT_THROW(db_.execute("PRAGMA database_list"), AuthError);
    EXPECT_EQ(count("users"), 2);
}

#ifdef SQLITEX_HAS_SESSION
TEST_F(PolicyTest, ReadOnlyAllowsSessionChangesetGeneration) {
    Session session(db_);
    session.attach("users");
    db_.execute("UPDATE users SET name='carol' WHERE id=1");
    ScopedPolicy policy(db_);
    policy.read_only();
    Changeset cs = session.changeset();
    EXPECT_GT(cs.size(), 0);
}
#endif

TEST_F(PolicyTest, AllowOnlyDeniesUnlistedAndIntersects) {
    ScopedPolicy policy(db_);
    policy.allow_only({Action::Select, Action::Read, Action::Insert});
    EXPECT_NO_THROW((void)db_.query("SELECT name FROM users"));
    EXPECT_NO_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES('x')"));
    EXPECT_THROW(db_.execute("DELETE FROM audit_log"), AuthError);
    EXPECT_THROW((void)db_.query("SELECT abs(1)"),
                 AuthError); // Function unlisted

    policy.allow_only({Action::Select, Action::Read});
    EXPECT_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES('y')"),
                 AuthError);
    EXPECT_NO_THROW((void)db_.query("SELECT name FROM users"));
}

TEST_F(PolicyTest, DenyTableMatchingRules) {
    db_.execute(
        "ATTACH ':memory:' AS aux;"
        "CREATE TABLE aux.audit_log(x);"
        "CREATE VIEW v_audit AS SELECT msg FROM audit_log;");
    {
        ScopedPolicy policy(db_);
        policy.deny_table("AUDIT_LOG", "main");
        EXPECT_THROW((void)db_.query("SELECT msg FROM audit_log"), AuthError);
        EXPECT_THROW((void)db_.query("SELECT msg FROM Main.Audit_Log"),
                     AuthError);
        EXPECT_THROW((void)db_.query("SELECT msg FROM v_audit"), AuthError);
        EXPECT_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES(1)"),
                     AuthError);
        // Unqualified count(*) reports no schema: fail closed.
        EXPECT_THROW((void)db_.query("SELECT count(*) FROM audit_log"),
                     AuthError);
        EXPECT_NO_THROW((void)db_.query("SELECT x FROM aux.audit_log"));
        EXPECT_NO_THROW((void)db_.query("SELECT name FROM users"));
        EXPECT_THROW(db_.execute("CREATE TEMP TRIGGER tt AFTER INSERT ON "
                                 "main.audit_log BEGIN SELECT 1; END"),
                     AuthError);
    }
    {
        ScopedPolicy policy(db_);
        policy.deny_table("v_audit");
        EXPECT_THROW((void)db_.query("SELECT msg FROM v_audit"), AuthError);
        EXPECT_NO_THROW((void)db_.query("SELECT msg FROM audit_log"));
    }
    {
        ScopedPolicy policy(db_);
        policy.deny_table("sqlite_schema");
        EXPECT_THROW((void)db_.query("SELECT name FROM sqlite_master"),
                     AuthError);
        EXPECT_THROW(db_.execute("CREATE TABLE t(x)"), AuthError);
    }
}

TEST_F(PolicyTest, DenyColumnBlocksReadUpdateAndAlter) {
    db_.execute("CREATE TABLE nn(id INTEGER PRIMARY KEY, c TEXT)");
    ScopedPolicy policy(db_);
    policy.deny_column("users", "password_hash")
        .deny_column("nn", "c")
        .deny_column("users", "id");
    EXPECT_THROW((void)db_.query("SELECT password_hash FROM users"), AuthError);
    EXPECT_THROW(db_.execute("UPDATE users SET password_hash='x'"), AuthError);
    EXPECT_THROW(db_.execute("ALTER TABLE users DROP COLUMN password_hash"),
                 AuthError);
    EXPECT_THROW(db_.execute("ALTER TABLE nn ALTER COLUMN c SET NOT NULL"),
                 AuthError);
    EXPECT_THROW((void)db_.query("SELECT rowid FROM users"), AuthError);
    EXPECT_NO_THROW((void)db_.query("SELECT name FROM users"));
}

TEST_F(PolicyTest, DenyFunctionThrowsAuthErrorAndSchemaLimit) {
    db_.execute(
        "CREATE TABLE g(a TEXT, b TEXT GENERATED ALWAYS AS (lower(a)) "
        "VIRTUAL);"
        "INSERT INTO g(a) VALUES('ABC');"
        "CREATE TABLE out(x);"
        "CREATE TRIGGER tf AFTER INSERT ON audit_log BEGIN "
        "INSERT INTO out VALUES(upper(new.msg)); END;");
    ScopedPolicy policy(db_);
    policy.deny_function("UPPER").deny_function("lower");
    EXPECT_THROW((void)db_.query("SELECT upper('x')"), AuthError);
    EXPECT_THROW(db_.execute("INSERT INTO audit_log(msg) VALUES('m')"),
                 AuthError);
    // Documented limit: VIRTUAL generated-column expressions are not
    // authorized at use time.
    auto v = db_.query_value<std::string>("SELECT b FROM g");
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, "abc");
}

TEST_F(PolicyTest, DenyPragmaIncludingTableValuedFunction) {
    ScopedPolicy policy(db_);
    policy.deny_pragma("TABLE_INFO");
    EXPECT_THROW((void)db_.query("PRAGMA table_info(users)"), AuthError);
    EXPECT_THROW((void)db_.query("SELECT * FROM pragma_table_info('users')"),
                 AuthError);
    EXPECT_NO_THROW((void)db_.query("PRAGMA user_version"));
}

// ============================================================================
// Ignore normalization
// ============================================================================

TEST_F(PolicyTest, IgnoreOutsideReadOnlyBecomesDeny) {
    auto ignore_update = [](const AuthorizerRequest& r) {
        return r.action == Action::Update ? Result::Ignore : Result::Allow;
    };
    {
        ScopedPolicy policy(db_, ignore_update);
        EXPECT_THROW(db_.execute("UPDATE users SET name='x'"), AuthError);
    }
    {
        ScopedPolicy policy(db_, ignore_update);
        policy.allow_only({Action::Update, Action::Read});
        EXPECT_THROW(db_.execute("UPDATE users SET name='x'"), AuthError);
    }
    {
        ScopedPolicy policy(db_);
        policy.add([](const AuthorizerRequest& r) {
            return r.action == Action::Read ? Result::Ignore : Result::Allow;
        });
        EXPECT_THROW((void)db_.query("SELECT ssn FROM users"), AuthError);
    }
    EXPECT_EQ(db_.query_value<std::string>("SELECT name FROM users WHERE id=1"),
              "alice");
}

TEST_F(PolicyTest, ReadOnlyRedactionHonorsIgnore) {
    ScopedPolicy policy(db_);
    policy.read_only().add([](const AuthorizerRequest& r) {
        if (r.action == Action::Read && r.arg1 == "users" && r.arg2 == "ssn")
            return Result::Ignore;
        if (r.action == Action::Function && r.arg2 == "hex")
            return Result::Ignore;
        return Result::Allow;
    });
    auto stmt = db_.prepare("SELECT name, ssn FROM users WHERE id=1");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_text(0), "alice");
    EXPECT_TRUE(stmt.column_is_null(1));

    auto f = db_.prepare("SELECT hex('a')");
    ASSERT_TRUE(f.step());
    EXPECT_TRUE(f.column_is_null(0));
}

TEST_F(PolicyTest, ReadOnlyIgnoreSelectYieldsNoRows) {
    ScopedPolicy policy(db_);
    policy.read_only().add([](const AuthorizerRequest& r) {
        return r.action == Action::Select ? Result::Ignore : Result::Allow;
    });
    EXPECT_TRUE(db_.query("SELECT name FROM users").empty());
}

#ifdef SQLITEX_HAS_SESSION
TEST_F(PolicyTest, RedactionInsideSessionBecomesDeny) {
    Session session(db_);
    session.attach("users");
    db_.execute("UPDATE users SET name='carol' WHERE id=1");
    ScopedPolicy policy(db_);
    policy.read_only().add([](const AuthorizerRequest& r) {
        return r.action == Action::Read && r.arg2 == "ssn" ? Result::Ignore
                                                           : Result::Allow;
    });
    EXPECT_THROW((void)session.changeset(), AuthError);
}
#endif

// ============================================================================
// Exception sinks
// ============================================================================

struct PolicyFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

AuthorizerCallback throw_on(Action action) {
    return [action](const AuthorizerRequest& r) -> Result {
        if (r.action == action)
            throw PolicyFailure("callback failed");
        return Result::Allow;
    };
}

TEST_F(PolicyTest, CallbackExceptionsAreRethrownFromEntryPoints) {
    auto stmt = db_.prepare("SELECT name FROM users");
    {
        ScopedPolicy policy(db_, throw_on(Action::Read));
        EXPECT_THROW((void)db_.prepare("SELECT name FROM users"),
                     PolicyFailure);
        EXPECT_THROW(db_.execute("SELECT name FROM users"), PolicyFailure);
        EXPECT_THROW(
            db_.exec("SELECT name FROM users",
                     [](std::span<const std::optional<std::string_view>>,
                        std::span<const std::string_view>) { return true; }),
            PolicyFailure);
        EXPECT_THROW((void)stmt.step(), PolicyFailure); // reprepare inside step
    }
    EXPECT_THROW(stmt.reset(), AuthError); // reports the failed step
    EXPECT_TRUE(stmt.step());
    EXPECT_EQ(count("users"), 2);
}

#ifdef SQLITEX_HAS_SESSION
TEST_F(PolicyTest, CallbackExceptionsFromSessionAndApply) {
    Session session(db_);
    session.attach("users");
    db_.execute("INSERT INTO users(name) VALUES('dave')");
    {
        ScopedPolicy policy(db_, throw_on(Action::Savepoint));
        EXPECT_THROW((void)session.changeset(), PolicyFailure);
    }
    Changeset cs = session.changeset();

    Database target(":memory:");
    target.execute(
        "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, "
        "password_hash TEXT, ssn TEXT)");
    {
        ScopedPolicy policy(target, throw_on(Action::Insert));
        EXPECT_THROW(
            cs.apply(target, [](ConflictType,
                                const auto&) { return ChangesetAction::Omit; }),
            PolicyFailure);
    }
    {
        ScopedPolicy policy(target);
        policy.deny_insert();
        EXPECT_THROW(
            cs.apply(target, [](ConflictType,
                                const auto&) { return ChangesetAction::Omit; }),
            AuthError);
    }
    EXPECT_NO_THROW(cs.apply(target, [](ConflictType, const auto&) {
        return ChangesetAction::Omit;
    }));
}
#endif

TEST_F(PolicyTest, RawHandleExceptionIsDroppedAsDeny) {
    ScopedPolicy policy(db_, throw_on(Action::Read));
    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(), "SELECT name FROM users", -1,
                                &raw, nullptr);
    EXPECT_EQ(rc, SQLITE_AUTH);
    sqlite3_finalize(raw);
    EXPECT_NO_THROW((void)db_.query("SELECT 1"));
}

TEST_F(PolicyTest, NestedOperationInUserFunctionContainsItsFailure) {
    bool inner_failed = false;
    db_.create_function("probe", 0,
                        [&](FunctionContext& ctx, std::span<const Value>) {
                            try {
                                (void)db_.prepare("SELECT ssn FROM users");
                            } catch (const PolicyFailure&) {
                                inner_failed = true;
                            }
                            ctx.set_result(int64_t{1});
                        });
    ScopedPolicy policy(db_, [](const AuthorizerRequest& r) -> Result {
        if (r.action == Action::Read && r.arg2 == "ssn")
            throw PolicyFailure("no ssn");
        return Result::Allow;
    });
    EXPECT_EQ(db_.query_value<int64_t>("SELECT probe()"), 1);
    EXPECT_TRUE(inner_failed);
}

TEST_F(PolicyTest, InvalidResultIsMisuse) {
    ScopedPolicy policy(
        db_, [](const AuthorizerRequest&) { return static_cast<Result>(42); });
    EXPECT_THROW((void)db_.query("SELECT 1"), MisuseError);
}

// ============================================================================
// BlobIO
// ============================================================================

TEST_F(PolicyTest, BlobIOOpenIsPolicyChecked) {
    db_.execute(
        "CREATE TABLE b(id INTEGER PRIMARY KEY, data BLOB, s BLOB);"
        "INSERT INTO b VALUES(1, x'0102', x'03');");
    BlobIO before(db_, "main", "b", "s", 1);
    {
        ScopedPolicy policy(db_);
        policy.read_only();
        EXPECT_NO_THROW(BlobIO(db_, "main", "b", "data", 1));
        EXPECT_THROW(
            BlobIO(db_, "main", "b", "data", 1, BlobOpenMode::ReadWrite),
            AuthError);
    }
    {
        ScopedPolicy policy(db_);
        policy.deny_column("b", "s");
        EXPECT_THROW(BlobIO(db_, "main", "b", "s", 1), AuthError);
        EXPECT_EQ(before.size(), 1); // opened before the scope
    }
    {
        ScopedPolicy policy(db_);
        policy.read_only().add([](const AuthorizerRequest& r) {
            return r.action == Action::Read ? Result::Ignore : Result::Allow;
        });
        EXPECT_THROW(BlobIO(db_, "main", "b", "data", 1), AuthError);
    }
}

// ============================================================================
// Hardening
// ============================================================================

int db_config(Database& db, int op) {
    int v = -1;
    sqlite3_db_config(db.handle(), op, -1, &v);
    return v;
}

TEST_F(PolicyTest, HardeningSettersApplyAndRestoreLifo) {
    int const defensive0 = db_config(db_, SQLITE_DBCONFIG_DEFENSIVE);
    int const trusted0 = db_config(db_, SQLITE_DBCONFIG_TRUSTED_SCHEMA);
    int const attach0 = sqlite3_limit(db_.handle(), SQLITE_LIMIT_ATTACHED, -1);
    {
        ScopedPolicy outer(db_);
        outer.defensive().trusted_schema(false).max_attached(1);
        EXPECT_EQ(db_config(db_, SQLITE_DBCONFIG_DEFENSIVE), 1);
        EXPECT_EQ(db_config(db_, SQLITE_DBCONFIG_TRUSTED_SCHEMA), 0);
        {
            ScopedPolicy inner(db_);
            // Inherits outer settings, then overrides.
            EXPECT_EQ(db_config(db_, SQLITE_DBCONFIG_DEFENSIVE), 1);
            inner.defensive(false).max_attached(0);
            EXPECT_EQ(db_config(db_, SQLITE_DBCONFIG_DEFENSIVE), 0);
            EXPECT_THROW(outer.defensive(), MisuseError);
        }
        EXPECT_EQ(db_config(db_, SQLITE_DBCONFIG_DEFENSIVE), 1);
        EXPECT_EQ(sqlite3_limit(db_.handle(), SQLITE_LIMIT_ATTACHED, -1), 1);
    }
    EXPECT_EQ(db_config(db_, SQLITE_DBCONFIG_DEFENSIVE), defensive0);
    EXPECT_EQ(db_config(db_, SQLITE_DBCONFIG_TRUSTED_SCHEMA), trusted0);
    EXPECT_EQ(sqlite3_limit(db_.handle(), SQLITE_LIMIT_ATTACHED, -1), attach0);
}

TEST_F(PolicyTest, HardeningEffects) {
    TempFile missing("missing");
    {
        ScopedPolicy policy(db_);
        policy.attach_create(false);
        EXPECT_ANY_THROW(db_.execute("ATTACH '" + missing.path() + "' AS aux"));
    }
    {
        ScopedPolicy policy(db_);
        policy.max_attached(0);
        EXPECT_ANY_THROW(db_.execute("ATTACH ':memory:' AS aux"));
        EXPECT_THROW(policy.max_attached(-1), MisuseError);
    }
    db_.execute(
        "CREATE TABLE hits(n);"
        "CREATE TRIGGER main_tr AFTER INSERT ON audit_log BEGIN "
        "INSERT INTO hits VALUES('main'); END;"
        "CREATE TEMP TRIGGER temp_tr AFTER INSERT ON audit_log BEGIN "
        "INSERT INTO hits VALUES('temp'); END;");
    {
        ScopedPolicy policy(db_);
        policy.triggers_enabled(false);
        db_.execute("INSERT INTO audit_log(msg) VALUES('x')");
    }
    EXPECT_EQ(db_.query("SELECT n FROM hits").size(), 1u);
    EXPECT_EQ(db_.query_value<std::string>("SELECT n FROM hits"), "temp");
    {
        ScopedPolicy policy(db_);
        policy.defensive();
        EXPECT_ANY_THROW(
            db_.execute("PRAGMA writable_schema=ON;"
                        "UPDATE sqlite_master SET sql=sql WHERE name='users'"));
    }
    db_.execute("PRAGMA writable_schema=OFF");

    db_.create_function("risky", 0,
                        [](FunctionContext& c, std::span<const Value>) {
                            c.set_result(int64_t{1});
                        });
    db_.create_function(
        "safe", 0,
        [](FunctionContext& c, std::span<const Value>) {
            c.set_result(int64_t{2});
        },
        FunctionFlag::Utf8 | FunctionFlag::Innocuous);
    db_.execute(
        "CREATE VIEW vr AS SELECT risky();"
        "CREATE VIEW vs AS SELECT safe();");
    {
        ScopedPolicy policy(db_);
        policy.trusted_schema(false);
        EXPECT_ANY_THROW((void)db_.query("SELECT * FROM vr"));
        EXPECT_EQ(db_.query_value<int64_t>("SELECT * FROM vs"), 2);
    }
    EXPECT_EQ(db_.query_value<int64_t>("SELECT * FROM vr"), 1);
}

// ============================================================================
// Misuse and lifetime
// ============================================================================

TEST_F(PolicyTest, MisuseIsRejected) {
    Database closed;
    EXPECT_THROW((void)ScopedPolicy{closed}, MisuseError);
    ScopedPolicy policy(db_);
    EXPECT_THROW(policy.add(AuthorizerCallback{}), MisuseError);
    EXPECT_THROW((void)ScopedPolicy(db_, AuthorizerCallback{}), MisuseError);
    // A failed constructor leaves the previous policy in place.
    policy.deny_delete();
    EXPECT_THROW(db_.execute("DELETE FROM audit_log"), AuthError);
}

#ifndef NDEBUG
TEST(PolicyDeathTest, NonLifoDestructionAsserts) {
    EXPECT_DEATH(
        {
            Database db(":memory:");
            auto outer = std::make_unique<ScopedPolicy>(db);
            auto inner = std::make_unique<ScopedPolicy>(db);
            outer.reset();
        },
        "LIFO");
}

TEST(PolicyDeathTest, LifecycleChangesWhileGuardedAssert) {
    EXPECT_DEATH(
        {
            Database db(":memory:");
            ScopedPolicy policy(db);
            db.close();
        },
        "ScopedPolicy");
    EXPECT_DEATH(
        {
            Database db(":memory:");
            ScopedPolicy policy(db);
            Database moved(std::move(db));
        },
        "ScopedPolicy");
    EXPECT_DEATH(
        {
            Database db(":memory:");
            Database other(":memory:");
            ScopedPolicy policy(other);
            db = std::move(other);
        },
        "ScopedPolicy");
}
#endif

} // namespace
} // namespace sqlitex

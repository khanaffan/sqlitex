#include <sqlitex/sqlitex.h>

#include <iostream>
#include <optional>
#include <string_view>

int main() {
    try {
        sqlitex::Database db(":memory:");
        db.execute(R"(
            CREATE TABLE accounts (
                id INTEGER PRIMARY KEY,
                username TEXT NOT NULL,
                api_secret TEXT NOT NULL
            )
        )");
        db.execute(
            "INSERT INTO accounts (username, api_secret) "
            "VALUES ('alice', 'sk-live-secret-1'), ('bob', "
            "'sk-live-secret-2')");

        // 1. Read-only hardened scope with column redaction via Ignore
        {
            sqlitex::ScopedPolicy policy(db);
            policy.read_only().defensive(true).trusted_schema(false).add(
                [](const sqlitex::AuthorizerRequest& req) {
                    if (req.action == sqlitex::AuthorizerAction::Read &&
                        req.arg1 == "accounts" && req.arg2 == "api_secret") {
                        return sqlitex::AuthorizerResult::Ignore;
                    }
                    return sqlitex::AuthorizerResult::Allow;
                });

            std::cout << "Inside read-only policy (api_secret redacted):\n";
            auto stmt = db.prepare(
                "SELECT username, api_secret FROM accounts ORDER BY id");
            for (auto [user, secret] :
                 stmt.rows<std::string_view,
                           std::optional<std::string_view>>()) {
                std::cout << "  " << user << ": "
                          << secret.value_or("<redacted>") << '\n';
            }

            // Attempted mutation is rejected with AuthError
            try {
                db.execute("DELETE FROM accounts WHERE username = 'bob'");
            } catch (const sqlitex::AuthError& e) {
                std::cout << "Blocked write as expected: " << e.what() << '\n';
            }
        }

        // 2. Policy guard destroyed: normal write access is automatically
        // restored
        db.execute(
            "INSERT INTO accounts (username, api_secret) "
            "VALUES ('charlie', 'sk-live-secret-3')");

        auto total = db.query_value<int64_t>("SELECT COUNT(*) FROM accounts")
                         .value_or(0);
        std::cout << "After policy exit, total accounts: " << total << '\n';
    } catch (const sqlitex::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

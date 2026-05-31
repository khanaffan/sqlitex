#include <cloudsqlite/cloudsqlite.h>

#include <iostream>

int main() {
    try {
        // Open an in-memory database
        cloudsqlite::Database db(":memory:");

        // Create a table
        db.execute(R"(
            CREATE TABLE users (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                email TEXT,
                age INTEGER
            )
        )");

        // Insert using prepared statement
        auto stmt = db.prepare("INSERT INTO users (name, email, age) VALUES (?, ?, ?)");
        stmt.bind_all(std::string_view("Alice"), std::string_view("alice@example.com"), 30);
        stmt.execute();

        stmt.reset();
        stmt.bind_all(std::string_view("Bob"), std::string_view("bob@example.com"), 25);
        stmt.execute();

        stmt.reset();
        stmt.bind_all(std::string_view("Charlie"), nullptr, 35);
        stmt.execute();

        // Query all rows
        auto rows = db.query("SELECT * FROM users ORDER BY id");
        for (const auto& row : rows) {
            std::cout << "User: " << row[1].as_text()
                      << ", Email: " << (row[2].is_null() ? "N/A" : row[2].as_text())
                      << ", Age: " << row[3].as_int64() << '\n';
        }

        // Convenience: scalar query
        auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM users");
        std::cout << "\nTotal users: " << count.value_or(0) << '\n';

        // Transaction
        {
            auto txn = cloudsqlite::Database::Transaction(db);
            db.execute("UPDATE users SET age = age + 1");
            txn.commit();
        }

        std::cout << "All users after age increment:\n";
        for (const auto& row : db.query("SELECT name, age FROM users")) {
            std::cout << "  " << row[0].as_text() << ": " << row[1].as_int64() << '\n';
        }

    } catch (const cloudsqlite::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

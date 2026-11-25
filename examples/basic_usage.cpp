#include <sqlitex/sqlitex.h>

#include <iostream>

int main() {
    try {
        // Open an in-memory database
        sqlitex::Database db(":memory:");

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
        auto stmt =
            db.prepare("INSERT INTO users (name, email, age) VALUES (?, ?, ?)");
        stmt.bind_all(std::string_view("Alice"),
                      std::string_view("alice@example.com"), 30);
        stmt.execute();

        stmt.reset();
        stmt.bind_all(std::string_view("Bob"),
                      std::string_view("bob@example.com"), 25);
        stmt.execute();

        stmt.reset();
        stmt.bind_all(std::string_view("Charlie"), nullptr, 35);
        stmt.execute();

        // Query all rows using C++20/23 typed row ranges
        auto select =
            db.prepare("SELECT name, email, age FROM users ORDER BY id");
        for (auto [name, email, age] :
             select.rows<std::string_view, std::optional<std::string_view>,
                         int64_t>()) {
            std::cout << "User: " << name
                      << ", Email: " << email.value_or("N/A")
                      << ", Age: " << age << '\n';
        }

        // Convenience: scalar query
        auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM users");
        std::cout << "\nTotal users: " << count.value_or(0) << '\n';

        // Transaction (RAII — rolls back automatically unless committed)
        {
            auto txn = db.transaction();
            db.execute("UPDATE users SET age = age + 1");
            txn.commit();
        }

        std::cout << "All users after age increment:\n";
        for (const auto& row : db.query("SELECT name, age FROM users")) {
            std::cout << "  " << row[0].get_text() << ": " << row[1].get_int()
                      << '\n';
        }

    } catch (const sqlitex::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

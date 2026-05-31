#include <cloudsqlite/cloudsqlite.h>

#include <iostream>

int main() {
    try {
        // Create three databases
        cloudsqlite::Database db1(":memory:");
        cloudsqlite::Database db2(":memory:");
        cloudsqlite::Database target(":memory:");

        // Set up identical schemas
        auto setup = [](cloudsqlite::Database& db) {
            db.execute(R"(
                CREATE TABLE employees (
                    id INTEGER PRIMARY KEY,
                    name TEXT NOT NULL,
                    department TEXT,
                    salary REAL
                )
            )");
        };
        setup(db1);
        setup(db2);
        setup(target);

        // --- DB1: Add two employees ---
        {
            cloudsqlite::Session s1(db1, "main");
            s1.attach_all();

            db1.execute("INSERT INTO employees VALUES (1, 'Alice', 'Engineering', 95000)");
            db1.execute("INSERT INTO employees VALUES (2, 'Bob', 'Sales', 65000)");

            // DB2: Add two different employees
            cloudsqlite::Session s2(db2, "main");
            s2.attach_all();

            db2.execute("INSERT INTO employees VALUES (3, 'Charlie', 'Engineering', 105000)");
            db2.execute("INSERT INTO employees VALUES (4, 'Diana', 'Marketing', 75000)");

            // Extract both changesets
            cloudsqlite::Changeset cs1 = s1.changeset();
            cloudsqlite::Changeset cs2 = s2.changeset();

            std::cout << "Changeset 1: " << cs1.size() << " bytes\n";
            std::cout << "Changeset 2: " << cs2.size() << " bytes\n";

            // --- Merge changesets ---
            cloudsqlite::Changegroup group;
            group.add(cs1);
            group.add(cs2);

            cloudsqlite::Changeset merged = group.output();
            std::cout << "Merged: " << merged.size() << " bytes\n\n";

            // Apply merged to target
            merged.apply(target,
                [](cloudsqlite::ConflictType, const cloudsqlite::ChangesetIterator&) {
                    return cloudsqlite::ChangesetAction::Abort;
                });

            std::cout << "Target after merged apply:\n";
            for (const auto& row : target.query("SELECT * FROM employees ORDER BY id")) {
                std::cout << "  " << row[0].as_int64() << ": " << row[1].as_text()
                          << " (" << row[2].as_text() << ") $"
                          << row[3].as_double() << '\n';
            }
        }

    } catch (const cloudsqlite::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

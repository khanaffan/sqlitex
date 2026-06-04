#include <sqlitex/sqlitex.h>

#include <iostream>
#include <vector>

int main() {
    try {
        // Create two databases: source and replica
        sqlitex::Database source(":memory:");
        sqlitex::Database replica(":memory:");

        // Set up identical schemas
        auto setup = [](sqlitex::Database& db) {
            db.execute(R"(
                CREATE TABLE items (
                    id INTEGER PRIMARY KEY,
                    name TEXT NOT NULL,
                    quantity INTEGER DEFAULT 0
                )
            )");
        };
        setup(source);
        setup(replica);

        // Seed the replica with initial data
        replica.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
        replica.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");

        // --- Start a session on the source database ---
        sqlitex::Session session(source, "main");
        session.attach_all(); // Track all tables

        // Make changes on source
        source.execute("INSERT INTO items VALUES (1, 'Widget', 100)"); // PK 1
        source.execute("INSERT INTO items VALUES (2, 'Gadget', 50)");  // PK 2
        source.execute(
            "INSERT INTO items VALUES (3, 'Doodad', 25)"); // PK 3 (new)
        source.execute("UPDATE items SET quantity = 150 WHERE id = 1");

        // Extract the changeset
        sqlitex::Changeset changeset = session.changeset();
        std::cout << "Changeset size: " << changeset.size() << " bytes\n\n";

        // --- Iterate over the changeset ---
        std::cout << "Changes in changeset:\n";
        auto iter = changeset.begin();
        while (iter.next()) {
            std::cout << "  Table: " << iter.table_name() << ", ";

            switch (iter.op()) {
            case sqlitex::ChangeOperation::Insert:
                std::cout << "INSERT";
                break;
            case sqlitex::ChangeOperation::Update:
                std::cout << "UPDATE";
                break;
            case sqlitex::ChangeOperation::Delete:
                std::cout << "DELETE";
                break;
            }

            std::cout << " (" << iter.column_count() << " columns)\n";

            // Print old/new values
            for (int i = 0; i < iter.column_count(); ++i) {
                auto old_val = iter.old_int(i);
                auto new_val = iter.new_int(i);
                if (old_val || new_val) {
                    std::cout << "    col[" << i << "]: "
                              << (old_val ? std::to_string(*old_val) : "NULL")
                              << " -> "
                              << (new_val ? std::to_string(*new_val) : "NULL")
                              << "\n";
                }
            }
        }

        // --- Apply changeset to replica ---
        std::cout << "\nApplying changeset to replica...\n";

        int conflict_count = 0;
        changeset.apply(replica, [&](sqlitex::ConflictType type,
                                     const sqlitex::ChangesetIterator& it) {
            std::cout << "  Conflict (" << static_cast<int>(type)
                      << ") on table " << it.table_name() << " — skipping\n";
            conflict_count++;
            return sqlitex::ChangesetAction::Omit;
        });

        std::cout << "Conflicts: " << conflict_count << "\n\n";

        // Verify replica now matches source
        std::cout << "Replica data after apply:\n";
        for (const auto& row :
             replica.query("SELECT * FROM items ORDER BY id")) {
            std::cout << "  id=" << row[0].as_int64()
                      << " name=" << row[1].as_text()
                      << " quantity=" << row[2].as_int64() << '\n';
        }

    } catch (const sqlitex::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

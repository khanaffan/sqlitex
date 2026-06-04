#include <sqlitex/sqlitex.h>

#include <iostream>
#include <string>

int main() {
    try {
        // Create a database with initial data
        sqlitex::Database db(":memory:");
        db.execute(R"(
            CREATE TABLE config (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            )
        )");
        db.execute("INSERT INTO config VALUES ('version', '1.0')");
        db.execute("INSERT INTO config VALUES ('theme', 'light')");
        db.execute("INSERT INTO config VALUES ('lang', 'en')");

        std::cout << "Initial config:\n";
        for (const auto& row : db.query("SELECT * FROM config ORDER BY key")) {
            std::cout << "  " << row[0].as_text() << " = " << row[1].as_text()
                      << '\n';
        }

        // Start a session to track changes
        sqlitex::Session session(db, "main");
        session.attach_all();

        // Make some changes
        db.execute("UPDATE config SET value = '2.0' WHERE key = 'version'");
        db.execute("DELETE FROM config WHERE key = 'lang'");
        db.execute("INSERT INTO config VALUES ('debug', 'true')");

        // Extract changeset
        sqlitex::Changeset cs = session.changeset();

        // --- Undo by inverting and applying back ---
        std::cout << "\nInverting and applying changeset to undo changes...\n";
        sqlitex::Changeset undo = cs.invert();

        undo.apply(
            db, [](sqlitex::ConflictType, const sqlitex::ChangesetIterator&) {
                // Abort on any conflict during undo
                return sqlitex::ChangesetAction::Abort;
            });

        std::cout << "\nConfig after undo:\n";
        for (const auto& row : db.query("SELECT * FROM config ORDER BY key")) {
            std::cout << "  " << row[0].as_text() << " = " << row[1].as_text()
                      << '\n';
        }

    } catch (const sqlitex::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

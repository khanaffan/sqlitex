#include <cloudsqlite/cloudsqlite.h>

#include <iostream>
#include <string>

int main() {
    try {
        // Create a database with initial data
        cloudsqlite::Database db(":memory:");
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
            std::cout << "  " << row[0].as_text() << " = " << row[1].as_text() << '\n';
        }

        // Start a session to track changes
        cloudsqlite::Session session(db, "main");
        session.attach_all();

        // Make some changes
        db.execute("UPDATE config SET value = '2.0' WHERE key = 'version'");
        db.execute("DELETE FROM config WHERE key = 'lang'");
        db.execute("INSERT INTO config VALUES ('debug', 'true')");

        // Extract changeset
        cloudsqlite::Changeset cs = session.changeset();

        // --- Undo by inverting and applying back ---
        std::cout << "\nInverting and applying changeset to undo changes...\n";
        cloudsqlite::Changeset undo = cs.invert();

        undo.apply(db,
            [](cloudsqlite::ConflictType, const cloudsqlite::ChangesetIterator&) {
                // Abort on any conflict during undo
                return cloudsqlite::ChangesetAction::Abort;
            });

        std::cout << "\nConfig after undo:\n";
        for (const auto& row : db.query("SELECT * FROM config ORDER BY key")) {
            std::cout << "  " << row[0].as_text() << " = " << row[1].as_text() << '\n';
        }

    } catch (const cloudsqlite::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

#include <sqlitex/sqlitex.h>

#include <filesystem>
#include <iostream>

/// Example demonstrating the bcvfs API surface.
///
/// NOTE: This example requires:
///   1. blockcachevfs.h / blockcachevfs.c from the SQLite source tree
///   2. Azure or Google Cloud storage credentials
///   3. The library must be compiled with SQLITEX_HAS_BCVFS enabled
///
/// For actual cloud-backed SQLite usage, replace the credential values below.

int main() {
    try {
        // --- 1. Create the Block Cache VFS ---
        std::cout << "Creating bcvfs...\n";
        sqlitex::Bcvfs bcvfs(
            std::filesystem::temp_directory_path() / "sqlite-block-cache",
            "bcvfs");

        // --- 2. Configure ---
        bcvfs
            .set_cache_size(256 * 1024 * 1024) // 256 MB cache
            .set_max_requests(8)               // 8 concurrent HTTP requests
            .set_http_timeout(std::chrono::seconds(30));

        // --- 3. Set auth callback ---
        bcvfs.set_auth_callback(
            [](std::string_view storage, std::string_view account,
               std::string_view container, std::string& token) -> bool {
                std::cout << "Auth requested for " << storage
                          << " account=" << account
                          << " container=" << container << '\n';

                // In production, retrieve an OAuth token or SAS token here.
                token = "your-auth-token-here";
                return true;
            });

        // --- 4. Attach a cloud container ---
        std::cout
            << "Attaching Azure container 'mycontainer' as alias 'mydata'...\n";
        bcvfs.attach("azure", "myaccount", "mycontainer", "mydata");

        // --- 5. Open a database via the bcvfs VFS ---
        std::cout << "Opening database...\n";
        sqlitex::Database db(
            "/mydata/mydatabase.db",
            sqlitex::OpenFlag::ReadWrite | sqlitex::OpenFlag::Create, "bcvfs");

        // --- 6. Use the database normally ---
        db.execute(R"(
            CREATE TABLE IF NOT EXISTS kv (
                key TEXT PRIMARY KEY,
                value TEXT
            )
        )");

        db.execute("INSERT OR REPLACE INTO kv VALUES ('hello', 'world')");

        auto rows = db.query("SELECT * FROM kv");
        for (const auto& row : rows) {
            std::cout << "  " << row[0].get_text() << " = " << row[1].get_text()
                      << '\n';
        }

        // --- 7. Upload changes to cloud ---
        std::cout << "Uploading changes...\n";
        int uploaded = bcvfs.upload("mydata");
        std::cout << "Uploaded " << uploaded << " database(s)\n";

        // --- 8. Register introspection virtual tables ---
        bcvfs.register_vtab(db);

        auto containers = db.query("SELECT * FROM bcv_container");
        std::cout << "Attached containers: " << containers.size() << '\n';

    } catch (const sqlitex::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

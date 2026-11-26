#include <sqlitex/sqlitex.h>

#include <iostream>
#include <string>
#include <string_view>

int main() {
    try {
        sqlitex::Database db(":memory:");
        db.execute(R"(
            CREATE TABLE products (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                price_cents INTEGER NOT NULL
            )
        )");

        // Configure the connection's built-in LRU statement cache
        db.set_statement_cache_capacity(16);
        auto& cache = db.statement_cache();

        // Repeated inserts reuse the same prepared statement automatically
        constexpr std::string_view kInsertSql =
            "INSERT INTO products (name, price_cents) VALUES (?, ?)";
        cache.execute(kInsertSql, "Keyboard", 7999);
        cache.execute(kInsertSql, "Mouse", 3999);
        cache.execute(kInsertSql, "Monitor", 29999);

        // Explicit CachedStatement lease returns itself to the cache on scope
        // exit (after automatically resetting and clearing bindings)
        constexpr std::string_view kSelectSql =
            "SELECT name, price_cents FROM products WHERE price_cents >= ? "
            "ORDER BY price_cents";

        for (int min_price : {5000, 10000}) {
            auto stmt = db.prepare_cached(kSelectSql);
            std::cout << "Query >= " << min_price << " cents (from_cache="
                      << (stmt.from_cache() ? "true" : "false") << "):\n";

            stmt->bind_all(min_price);
            for (auto [name, price] : stmt->rows<std::string_view, int64_t>()) {
                std::cout << "  " << name << ": $" << (price / 100) << "."
                          << (price % 100 < 10 ? "0" : "") << (price % 100)
                          << '\n';
            }
        }

        // Scalar lookup via the cache
        auto avg_price =
            cache.query_value<double>("SELECT AVG(price_cents) FROM products");
        std::cout << "\nAverage price (cents): " << avg_price.value_or(0.0)
                  << '\n';

        // Inspect cache telemetry
        auto stats = cache.stats();
        std::cout << "Cache stats: size=" << cache.size()
                  << ", hits=" << stats.hits << ", misses=" << stats.misses
                  << ", evictions=" << stats.evictions
                  << ", bypasses=" << stats.bypasses << '\n';

        // Standalone StatementCache with tight capacity demonstrating eviction
        sqlitex::StatementCache small_cache(db, 2);
        (void)small_cache.query_value<int64_t>("SELECT 1");
        (void)small_cache.query_value<int64_t>("SELECT 2");
        (void)small_cache.query_value<int64_t>("SELECT 3");
        std::cout << "Standalone cache (cap=2): size=" << small_cache.size()
                  << ", evictions=" << small_cache.stats().evictions << '\n';
    } catch (const sqlitex::Error& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace sqlitex {
namespace {

// ============================================================================
// R-Tree geometry & scored query callbacks
// ============================================================================

TEST(RTreeExtensionTest, GeometryAndQueryCallbacks) {
    static std::atomic<int> query_user_data_freed{0};
    query_user_data_freed.store(0);

    {
        Database db(":memory:");
        db.execute(
            "CREATE VIRTUAL TABLE boxes USING rtree(id, minX, maxX, "
            "minY, maxY);");
        db.execute(
            "INSERT INTO boxes VALUES "
            "(1, 0.0, 2.0, 0.0, 2.0),"
            "(2, 10.0, 12.0, 10.0, 12.0),"
            "(3, 1.0, 3.0, 1.0, 3.0);");

        // 1st-generation geometry callback: circle(cx, cy, r)
        register_rtree_geometry_callback(
            db, "circle",
            [](std::span<const double> params,
               std::span<const double> coords) -> bool {
                if (params.size() != 3 || coords.size() != 4) {
                    return false;
                }
                double cx = params[0];
                double cy = params[1];
                double r = params[2];
                double near_x = std::clamp(cx, coords[0], coords[1]);
                double near_y = std::clamp(cy, coords[2], coords[3]);
                double dx = cx - near_x;
                double dy = cy - near_y;
                return (dx * dx + dy * dy) <= (r * r);
            });

        auto rows = db.query(
            "SELECT id FROM boxes WHERE id MATCH "
            "circle(1.5, 1.5, 1.0) ORDER BY id;");
        ASSERT_EQ(rows.size(), 2U);
        EXPECT_EQ(rows[0][0].as_int64(), 1);
        EXPECT_EQ(rows[1][0].as_int64(), 3);

        // 2nd-generation scored query callback: nearest(px, py)
        register_rtree_query_callback(db, "nearest", [](RTreeQueryInfo& info) {
            if (info.user_data() == nullptr) {
                info.set_user_data(new int(42), [](void* p) {
                    delete static_cast<int*>(p);
                    query_user_data_freed.fetch_add(1);
                });
            }
            EXPECT_EQ(info.param_count(), 2);
            EXPECT_EQ(info.coord_count(), 4);
            double px = info.param(0);
            double py = info.param(1);
            double near_x = std::clamp(px, info.coord(0), info.coord(1));
            double near_y = std::clamp(py, info.coord(2), info.coord(3));
            double dx = px - near_x;
            double dy = py - near_y;
            double dist = std::sqrt(dx * dx + dy * dy);
            info.set_score(dist);
            info.set_within(RTreeWithin::PartlyWithin);
        });

        auto nearest = db.query(
            "SELECT id FROM boxes WHERE id MATCH "
            "nearest(11.0, 11.0) LIMIT 1;");
        ASSERT_EQ(nearest.size(), 1U);
        EXPECT_EQ(nearest[0][0].as_int64(), 2);
    }

    EXPECT_GE(query_user_data_freed.load(), 1);
}

// ============================================================================
// FTS5 custom tokenizer (v2) & custom auxiliary function
// ============================================================================

class WhitespaceLowerTokenizer final : public Fts5Tokenizer {
    public:
    explicit WhitespaceLowerTokenizer(std::string prefix)
        : prefix_(std::move(prefix)) {
    }

    int tokenize(Fts5TokenizeFlags /*flags*/, std::string_view text,
                 std::string_view /*locale*/,
                 const Fts5TokenEmit& emit) override {
        size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[i])) != 0) {
                ++i;
            }
            size_t start = i;
            while (i < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[i])) == 0) {
                ++i;
            }
            if (i > start) {
                std::string tok = prefix_;
                for (size_t k = start; k < i; ++k) {
                    tok.push_back(static_cast<char>(
                        std::tolower(static_cast<unsigned char>(text[k]))));
                }
                int rc = emit(Fts5TokenFlags{}, tok, static_cast<int>(start),
                              static_cast<int>(i));
                if (rc != 0) {
                    return rc;
                }
            }
        }
        return 0;
    }

    private:
    std::string prefix_;
};

TEST(Fts5ExtensionTest, CustomTokenizerAndAuxiliaryFunction) {
    Database db(":memory:");
    Fts5Api api = Fts5Api::from_database(db);
    ASSERT_TRUE(api);
    EXPECT_GE(api.version(), 2);

    // Register custom tokenizer
    api.create_tokenizer(
        "ws_lower", [](std::span<const std::string_view> args) {
            std::string prefix = args.empty() ? "" : std::string(args[0]);
            return std::make_unique<WhitespaceLowerTokenizer>(
                std::move(prefix));
        });

    EXPECT_TRUE(api.has_tokenizer("ws_lower"));
    EXPECT_TRUE(api.has_tokenizer("unicode61"));
    EXPECT_FALSE(api.has_tokenizer("no_such_tokenizer_xyz"));

    // Direct tokenize_with
    std::vector<std::string> direct_tokens;
    api.tokenize_with("ws_lower", {"p_"}, Fts5TokenizeFlag::Document,
                      "Hello WORLD",
                      [&](Fts5TokenFlags /*tflags*/, std::string_view tok,
                          int /*start*/, int /*end*/) {
                          direct_tokens.emplace_back(tok);
                          return 0;
                      });
    ASSERT_EQ(direct_tokens.size(), 2U);
    EXPECT_EQ(direct_tokens[0], "p_hello");
    EXPECT_EQ(direct_tokens[1], "p_world");

    // Register custom auxiliary function exercising Fts5ExtensionContext
    static std::atomic<int> aux_freed{0};
    aux_freed.store(0);
    api.create_function("match_stats", [](const Fts5ExtensionContext& fts5,
                                          FunctionContext& sql_ctx,
                                          std::span<const ValueRef> args) {
        if (fts5.get_auxdata() == nullptr) {
            fts5.set_auxdata(new int(1), [](void* p) {
                delete static_cast<int*>(p);
                aux_freed.fetch_add(1);
            });
        }
        EXPECT_EQ(fts5.column_count(), 2);
        EXPECT_GE(fts5.row_count(), 1);
        EXPECT_GE(fts5.column_total_size(0), 1);
        EXPECT_GE(fts5.phrase_count(), 1);
        EXPECT_GE(fts5.phrase_size(0), 1);
        int hits = fts5.inst_count();
        int col_occ = 0;
        fts5.for_each_phrase_occurrence(
            0, [&](int /*col*/, int /*off*/) { ++col_occ; });
        EXPECT_EQ(col_occ, hits);

        int64_t bonus = args.empty() ? 0 : args[0].as_int64();
        sql_ctx.set_result(static_cast<int64_t>(hits * 100) +
                           fts5.column_size(1) + bonus);
    });

    db.execute(
        "CREATE VIRTUAL TABLE docs USING fts5("
        "title, body, tokenize='ws_lower');");
    db.execute(
        "INSERT INTO docs VALUES ('SQLite Guide', 'SQLite is fast and "
        "reliable SQLite');");
    db.execute("INSERT INTO docs VALUES ('Other', 'Nothing here');");

    auto rows = db.query(
        "SELECT rowid, match_stats(docs, 7) FROM docs "
        "WHERE docs MATCH 'sqlite';");
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0][0].as_int64(), 1);
    // 3 hits ('SQLite' in title + 2 in body) * 100 + 6 tokens in body + 7 = 313
    EXPECT_EQ(rows[0][1].as_int64(), 313);
    EXPECT_GE(aux_freed.load(), 1);
}

} // namespace
} // namespace sqlitex

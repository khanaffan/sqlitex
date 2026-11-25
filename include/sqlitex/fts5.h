#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/function.h"
#include "sqlitex/value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

class Database;

// ============================================================================
// FTS5 tokenizer flags
// ============================================================================

enum class Fts5TokenizeFlag : int {
    Query = 0x0001,
    Prefix = 0x0002,
    Document = 0x0004,
    Aux = 0x0008,
};

constexpr int kFts5TokenColocated = 0x0001;

/// Token callback signature used both by custom tokenizers and by
/// Fts5ExtensionContext::tokenize / Fts5Api::tokenize_with.
/// Return 0 (SQLITE_OK) to continue, or a non-zero SQLite code to stop.
using Fts5TokenEmit = std::function<int(int token_flags, std::string_view token,
                                        int start_offset, int end_offset)>;

// ============================================================================
// Fts5Tokenizer — abstract base for custom FTS5 tokenizers
// ============================================================================

class SQLITEX_EXPORT Fts5Tokenizer {
    public:
    virtual ~Fts5Tokenizer() = default;

    /// Tokenize UTF-8 text (with optional locale from FTS5 v2 API).
    /// Invoke `emit(tflags, token, start_byte, end_byte)` for each token.
    /// If `emit` returns non-zero, stop immediately and return that value.
    /// Return 0 (SQLITE_OK) on success.
    virtual int tokenize(int flags, std::string_view text,
                         std::string_view locale,
                         const Fts5TokenEmit& emit) = 0;
};

/// Factory creating a custom tokenizer instance from CREATE VIRTUAL TABLE args.
using Fts5TokenizerFactory = std::function<std::unique_ptr<Fts5Tokenizer>(
    const std::vector<std::string_view>& args)>;

// ============================================================================
// Fts5Instance — match instance metadata from xInst
// ============================================================================

struct Fts5Instance {
    int phrase = 0;
    int column = 0;
    int offset = 0;
};

// ============================================================================
// Fts5ExtensionContext — non-owning wrapper for Fts5ExtensionApi + Fts5Context
// ============================================================================

class SQLITEX_EXPORT Fts5ExtensionContext {
    public:
    Fts5ExtensionContext(const Fts5ExtensionApi* api, Fts5Context* ctx) noexcept
        : api_(api), ctx_(ctx) {
    }

    int api_version() const noexcept;
    int column_count() const noexcept;
    int64_t row_count() const;
    int64_t column_total_size(int col) const;

    int phrase_count() const noexcept;
    int phrase_size(int phrase) const noexcept;

    int inst_count() const;
    Fts5Instance inst(int idx) const;

    int64_t rowid() const noexcept;
    std::string_view column_text(int col) const;
    int column_size(int col) const;
    std::string_view column_locale(int col) const;

    std::string_view query_token(int phrase, int token) const;
    std::string_view inst_token(int idx, int token) const;

    void tokenize(std::string_view text, const Fts5TokenEmit& on_token) const;
    void tokenize(std::string_view text, std::string_view locale,
                  const Fts5TokenEmit& on_token) const;

    void for_each_phrase_occurrence(
        int phrase, const std::function<void(int col, int offset)>& fn) const;
    void for_each_phrase_column(int phrase,
                                const std::function<void(int col)>& fn) const;
    void query_phrase(
        int phrase,
        const std::function<void(const Fts5ExtensionContext&)>& fn) const;

    void set_auxdata(void* data, void (*destroy)(void*) = nullptr) const;
    void* get_auxdata(bool clear = false) const noexcept;

    const Fts5ExtensionApi* api() const noexcept {
        return api_;
    }
    Fts5Context* handle() const noexcept {
        return ctx_;
    }

    private:
    const Fts5ExtensionApi* api_;
    Fts5Context* ctx_;
};

/// Custom FTS5 auxiliary function callback.
using Fts5AuxFunction = std::function<void(const Fts5ExtensionContext& fts5,
                                           FunctionContext& sql_ctx,
                                           const std::vector<ValueRef>& args)>;

// ============================================================================
// Fts5Api — non-owning wrapper for fts5_api* (bound to a Database)
// ============================================================================

class SQLITEX_EXPORT Fts5Api {
    public:
    /// Obtain the FTS5 extension API handle for a database connection.
    /// Throws Error if FTS5 is unavailable.
    static Fts5Api from_database(Database& db);

    explicit operator bool() const noexcept {
        return api_ != nullptr;
    }

    int version() const noexcept;

    /// Register a custom FTS5 tokenizer (uses v2 API when available).
    void create_tokenizer(std::string_view name, Fts5TokenizerFactory factory);

    /// Check whether a tokenizer with the given name is registered.
    bool has_tokenizer(std::string_view name) const;

    /// Invoke a registered tokenizer by name on input text (uses
    /// xFindTokenizer_v2 when available, falling back to xFindTokenizer).
    void tokenize_with(std::string_view tokenizer_name,
                       const std::vector<std::string>& args, int flags,
                       std::string_view text, const Fts5TokenEmit& on_token,
                       std::string_view locale = {}) const;

    /// Register a custom FTS5 auxiliary function.
    void create_function(std::string_view name, Fts5AuxFunction fn);

    fts5_api* handle() const noexcept {
        return api_;
    }

    private:
    explicit Fts5Api(fts5_api* api) noexcept : api_(api) {
    }

    fts5_api* api_ = nullptr;
};

} // namespace sqlitex

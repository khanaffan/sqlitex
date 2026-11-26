#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/error.h"
#include "sqlitex/flags.h"
#include "sqlitex/value.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace sqlitex {

class Database;

// ============================================================================
// Statement enums & scan-status descriptors
// ============================================================================

enum class PrepareFlag : unsigned int {
    None = 0,
    Persistent = SQLITE_PREPARE_PERSISTENT,
    Normalize = SQLITE_PREPARE_NORMALIZE,
    NoVtab = SQLITE_PREPARE_NO_VTAB,
    DontLog = SQLITE_PREPARE_DONT_LOG,
    FromDdl = SQLITE_PREPARE_FROM_DDL,
};

template <>
struct EnableBitmaskOperators<PrepareFlag> : std::true_type {};

using PrepareFlags = Flags<PrepareFlag>;

enum class StmtStatusOp : int {
    FullscanStep = SQLITE_STMTSTATUS_FULLSCAN_STEP,
    Sort = SQLITE_STMTSTATUS_SORT,
    AutoIndex = SQLITE_STMTSTATUS_AUTOINDEX,
    VmStep = SQLITE_STMTSTATUS_VM_STEP,
    Reprepare = SQLITE_STMTSTATUS_REPREPARE,
    Run = SQLITE_STMTSTATUS_RUN,
    FilterMiss = SQLITE_STMTSTATUS_FILTER_MISS,
    FilterHit = SQLITE_STMTSTATUS_FILTER_HIT,
    MemUsed = SQLITE_STMTSTATUS_MEMUSED,
};

enum class ExplainMode : int {
    Normal = 0,
    Explain = 1,
    QueryPlan = 2,
};

enum class CArrayType : int {
    Int32 = SQLITE_CARRAY_INT32,
    Int64 = SQLITE_CARRAY_INT64,
    Double = SQLITE_CARRAY_DOUBLE,
    Text = SQLITE_CARRAY_TEXT,
    Blob = SQLITE_CARRAY_BLOB,
};

enum class ScanStatusFlag : int {
    None = 0,
    Complex = SQLITE_SCANSTAT_COMPLEX,
};

template <>
struct EnableBitmaskOperators<ScanStatusFlag> : std::true_type {};

using ScanStatusFlags = Flags<ScanStatusFlag>;

struct ScanStatusEntry {
    int64_t nloop = 0;
    int64_t nvisit = 0;
    double est = 0.0;
    std::string name;
    std::string explain;
    int select_id = 0;
    int parent_id = 0;
    int64_t ncycle = 0;
};

// Forward declarations for C++20 range views
class RowView;
class RowRange;
template <ColumnValue... Ts>
class TypedRowRange;

// ============================================================================
// Statement — RAII wrapper for sqlite3_stmt*
// ============================================================================

class SQLITEX_EXPORT Statement {
    public:
    Statement() noexcept = default;

    /// Construct from a prepared statement.
    /// Prefer Database::prepare() over direct construction.
    Statement(sqlite3_stmt* stmt, Database* db) noexcept;

    ~Statement();

    // Move-only
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    explicit operator bool() const noexcept {
        return stmt_ != nullptr;
    }

    sqlite3_stmt* handle() noexcept {
        return stmt_;
    }
    const sqlite3_stmt* handle() const noexcept {
        return stmt_;
    }

    // === Parameter Binding (returns *this for chaining) ===

    Statement& bind(int index, std::nullptr_t);
    Statement& bind(int index, std::nullopt_t) {
        return bind(index, nullptr);
    }
    Statement& bind(int index, bool value) {
        return bind(index, static_cast<int32_t>(value ? 1 : 0));
    }
    Statement& bind(int index, int32_t value);
    Statement& bind(int index, int64_t value);

    template <SqlInteger T>
        requires(!std::same_as<T, int32_t> && !std::same_as<T, int64_t>)
    Statement& bind(int index, T value) {
        return bind(index, static_cast<int64_t>(value));
    }

    Statement& bind(int index, double value);

    template <SqlFloat T>
        requires(!std::same_as<T, double>)
    Statement& bind(int index, T value) {
        return bind(index, static_cast<double>(value));
    }

    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, const char* value) {
        if (value == nullptr) {
            return bind(index, nullptr);
        }
        return bind(index, std::string_view(value));
    }

    Statement& bind(int index, std::span<const std::byte> blob);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    Statement& bind(int index, const R& blob) {
        return bind(index, to_const_bytes(blob));
    }

    Statement& bind(int index, const Value& value);

    template <Bindable T>
    Statement& bind(int index, const std::optional<T>& value) {
        if (!value.has_value()) {
            return bind(index, nullptr);
        }
        return bind(index, *value);
    }

    /// Bind a zero-filled BLOB (sqlite3_bind_zeroblob / 64).
    Statement& bind_zeroblob(int index, int size);
    Statement& bind_zeroblob64(int index, uint64_t size);

    /// Bind static text or blob without copying (SQLITE_STATIC).
    /// Caller guarantees lifetime exceeds statement execution.
    Statement& bind_static(int index, std::string_view value);
    Statement& bind_static(int index, std::span<const std::byte> blob);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    Statement& bind_static(int index, const R& blob) {
        return bind_static(index, to_const_bytes(blob));
    }

    /// Bind 64-bit sized BLOB or text with optional custom destructor.
    /// If destructor is nullptr, SQLite makes a transient copy.
    Statement& bind_blob64(int index, std::span<const std::byte> blob,
                           void (*destructor)(void*) = nullptr);
    Statement& bind_text64(int index, std::string_view text,
                           unsigned char encoding = SQLITE_UTF8,
                           void (*destructor)(void*) = nullptr);

    /// Bind an opaque pointer (sqlite3_bind_pointer).
    /// `type_name` must be a static string literal.
    Statement& bind_pointer(int index, void* ptr, const char* type_name,
                            void (*destructor)(void*) = nullptr);

    /// Type-safe owning pointer binding; SQLite takes ownership and destroys
    /// via delete_typed<T> (even if binding fails).
    template <typename T>
    Statement& bind_pointer(int index, std::unique_ptr<T> ptr,
                            const char* type_name) {
        return bind_pointer(index, ptr.release(), type_name,
                            &detail::delete_typed<T>);
    }

    /// Bind a sqlite3_value* via ValueRef or ProtectedValue
    /// (sqlite3_bind_value).
    Statement& bind_value(int index, const ValueRef& val);
    Statement& bind_value(int index, const ProtectedValue& val);

    /// Bind an array to a carray() table-valued function parameter
    /// (sqlite3_carray_bind / sqlite3_carray_bind_v2).
    Statement& bind_carray(int index, void* data, int count, CArrayType type,
                           void (*destructor)(void*) = nullptr);
    Statement& bind_carray_v2(int index, void* data, int count, CArrayType type,
                              void (*destructor)(void*), void* del_arg);
    Statement& bind_carray(int index, std::span<const int32_t> values);
    Statement& bind_carray(int index, std::span<const int64_t> values);
    Statement& bind_carray(int index, std::span<const double> values);
    Statement& bind_carray(int index, std::span<const std::string> values);
    Statement& bind_carray(int index, std::span<const std::string_view> values);

    /// Bind by named parameter. Accepts any Bindable type.
    template <Bindable T>
    Statement& bind(std::string_view name, T&& value) {
        int const idx = bind_parameter_index(name);
        return bind(idx, std::forward<T>(value));
    }

    /// Bind all positional parameters from a parameter pack (1-indexed).
    template <Bindable... Args>
    Statement& bind_all(Args&&... args);

    // === Execution ===

    /// Advance to the next row.
    /// Returns true if a row is available, false when done. Throws on error.
    [[nodiscard]] bool step();

    /// Non-throwing step returning std::expected<bool, Error>.
    [[nodiscard]] Expected<bool> try_step();

    /// Convenience: call step() once, discarding any row.
    /// Returns true if rows remain.
    bool execute();

    /// Non-throwing execute returning std::expected<bool, Error>.
    [[nodiscard]] Expected<bool> try_execute();

    /// Reset the statement so it can be re-executed.
    void reset();

    /// Clear all parameter bindings.
    void clear_bindings();

    // === Range-based Row Iteration (C++20 std::ranges::input_range) ===

    /// Return a single-pass input range of RowView objects over remaining rows.
    [[nodiscard]] RowRange rows() noexcept;

    /// Return a single-pass input range yielding T (for 1 type) or
    /// std::tuple<Ts...> (for 2+ types) extracted from columns 0..N-1.
    template <ColumnValue... Ts>
        requires(sizeof...(Ts) >= 1)
    [[nodiscard]] TypedRowRange<Ts...> rows() noexcept;

    // === Column Access ===

    [[nodiscard]] int column_count() const noexcept;
    [[nodiscard]] int data_count() const noexcept;
    [[nodiscard]] Value::Type column_type(int index) const noexcept;
    [[nodiscard]] std::string_view column_name(int index) const noexcept;
    [[nodiscard]] std::string_view column_decltype(int index) const noexcept;
    [[nodiscard]] std::string_view column_database_name(
        int index) const noexcept;
    [[nodiscard]] std::string_view column_table_name(int index) const noexcept;
    [[nodiscard]] std::string_view column_origin_name(int index) const noexcept;

    [[nodiscard]] Value column_value(int index) const;
    [[nodiscard]] ValueRef column_raw_value(int index) const noexcept;

    [[nodiscard]] int32_t column_int(int index) const;
    [[nodiscard]] int64_t column_int64(int index) const;
    [[nodiscard]] double column_double(int index) const;
    [[nodiscard]] std::string_view column_text(int index) const;
    [[nodiscard]] std::span<const std::byte> column_blob(int index) const;
    [[nodiscard]] int column_bytes(int index) const noexcept;

    [[nodiscard]] bool column_is_null(int index) const noexcept;

    /// Null-checked column access: returns nullopt for SQL NULL.
    [[nodiscard]] std::optional<int64_t> column_optional_int64(int index) const;
    [[nodiscard]] std::optional<double> column_optional_double(int index) const;
    [[nodiscard]] std::optional<std::string_view> column_optional_text(
        int index) const;
    [[nodiscard]] std::optional<std::span<const std::byte>>
    column_optional_blob(int index) const;

    /// Typed column extraction constrained by ColumnValue.
    template <ColumnValue T>
    [[nodiscard]] T column_as(int index) const;

    // === Parameter Metadata ===

    [[nodiscard]] int bind_parameter_count() const noexcept;
    [[nodiscard]] int bind_parameter_index(std::string_view name) const;
    [[nodiscard]] std::string_view bind_parameter_name(int index) const;

    // === Statement Metadata ===

    [[nodiscard]] bool is_busy() const noexcept;
    [[nodiscard]] bool is_readonly() const noexcept;
    [[nodiscard]] bool is_explain() const noexcept;
    [[nodiscard]] ExplainMode explain_mode() const noexcept;
    void set_explain(ExplainMode mode);

    [[nodiscard]] std::string_view sql() const noexcept;
    [[nodiscard]] std::string expanded_sql() const;
    [[nodiscard]] std::string_view normalized_sql() const noexcept;

    /// Query a prepared-statement status counter (sqlite3_stmt_status).
    [[nodiscard]] int status(StmtStatusOp op, bool reset = false) noexcept;

    /// Collect query-loop execution statistics (sqlite3_stmt_scanstatus_v2).
    [[nodiscard]] std::vector<ScanStatusEntry> scanstatus(
        ScanStatusFlags flags = ScanStatusFlag::None) const;

    /// Reset scanstatus counters to zero (sqlite3_stmt_scanstatus_reset).
    void scanstatus_reset() noexcept;

    // === Database access ===

    Database& database() {
        if (db_ == nullptr) {
            throw std::logic_error(
                "Statement::database() called on a null statement");
        }
        return *db_;
    }
    const Database& database() const {
        if (db_ == nullptr) {
            throw std::logic_error(
                "Statement::database() called on a null statement");
        }
        return *db_;
    }

    private:
    sqlite3_stmt* stmt_ = nullptr;
    Database* db_ = nullptr;

    friend class Database;
    friend class StatementCache;
};

// ============================================================================
// RowView / RowRange / TypedRowRange (C++20 std::ranges::input_range views)
// ============================================================================

class RowView {
    public:
    explicit RowView(const Statement& stmt) noexcept : stmt_(&stmt) {
    }

    [[nodiscard]] int column_count() const noexcept {
        return stmt_->column_count();
    }
    [[nodiscard]] int data_count() const noexcept {
        return stmt_->data_count();
    }
    [[nodiscard]] Value::Type type(int index) const noexcept {
        return stmt_->column_type(index);
    }
    [[nodiscard]] bool is_null(int index) const noexcept {
        return stmt_->column_is_null(index);
    }
    [[nodiscard]] std::string_view name(int index) const noexcept {
        return stmt_->column_name(index);
    }

    [[nodiscard]] int32_t get_int(int index) const {
        return stmt_->column_int(index);
    }
    [[nodiscard]] int64_t get_int64(int index) const {
        return stmt_->column_int64(index);
    }
    [[nodiscard]] double get_double(int index) const {
        return stmt_->column_double(index);
    }
    [[nodiscard]] std::string_view get_text(int index) const {
        return stmt_->column_text(index);
    }
    [[nodiscard]] std::span<const std::byte> get_blob(int index) const {
        return stmt_->column_blob(index);
    }
    [[nodiscard]] Value get_value(int index) const {
        return stmt_->column_value(index);
    }
    [[nodiscard]] ValueRef get_raw_value(int index) const noexcept {
        return stmt_->column_raw_value(index);
    }

    template <ColumnValue T>
    [[nodiscard]] T get(int index) const {
        return stmt_->column_as<T>(index);
    }

    template <ColumnValue... Ts>
        requires(sizeof...(Ts) >= 1)
    [[nodiscard]] std::tuple<Ts...> as_tuple() const {
        return as_tuple_impl<Ts...>(std::index_sequence_for<Ts...>{});
    }

    [[nodiscard]] const Statement& statement() const noexcept {
        return *stmt_;
    }

    private:
    template <ColumnValue... Ts, std::size_t... Is>
    std::tuple<Ts...> as_tuple_impl(std::index_sequence<Is...>) const {
        return std::tuple<Ts...>(stmt_->column_as<Ts>(static_cast<int>(Is))...);
    }

    const Statement* stmt_ = nullptr;
};

class RowRange : public std::ranges::view_interface<RowRange> {
    public:
    class Iterator {
        public:
        using iterator_concept = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = RowView;

        Iterator() noexcept = default;
        explicit Iterator(Statement* stmt) noexcept : stmt_(stmt) {
        }

        RowView operator*() const noexcept {
            return RowView(*stmt_);
        }

        Iterator& operator++() {
            if (stmt_ != nullptr && !stmt_->step()) {
                stmt_ = nullptr;
            }
            return *this;
        }

        void operator++(int) {
            ++(*this);
        }

        friend bool operator==(const Iterator& it,
                               std::default_sentinel_t) noexcept {
            return it.stmt_ == nullptr;
        }

        private:
        Statement* stmt_ = nullptr;
    };

    RowRange() noexcept = default;
    explicit RowRange(Statement& stmt) noexcept : stmt_(&stmt) {
    }

    Iterator begin() {
        if (stmt_ != nullptr && stmt_->step()) {
            return Iterator(stmt_);
        }
        return Iterator(nullptr);
    }

    std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

    private:
    Statement* stmt_ = nullptr;
};

template <ColumnValue... Ts>
class TypedRowRange : public std::ranges::view_interface<TypedRowRange<Ts...>> {
    public:
    using RowType =
        std::conditional_t<sizeof...(Ts) == 1,
                           std::tuple_element_t<0, std::tuple<Ts...>>,
                           std::tuple<Ts...>>;

    class Iterator {
        public:
        using iterator_concept = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = RowType;

        Iterator() noexcept = default;
        explicit Iterator(Statement* stmt) noexcept : stmt_(stmt) {
        }

        RowType operator*() const {
            if constexpr (sizeof...(Ts) == 1) {
                return stmt_->column_as<RowType>(0);
            } else {
                return extract(std::index_sequence_for<Ts...>{});
            }
        }

        Iterator& operator++() {
            if (stmt_ != nullptr && !stmt_->step()) {
                stmt_ = nullptr;
            }
            return *this;
        }

        void operator++(int) {
            ++(*this);
        }

        friend bool operator==(const Iterator& it,
                               std::default_sentinel_t) noexcept {
            return it.stmt_ == nullptr;
        }

        private:
        template <std::size_t... Is>
        std::tuple<Ts...> extract(std::index_sequence<Is...>) const {
            return std::tuple<Ts...>(
                stmt_->column_as<Ts>(static_cast<int>(Is))...);
        }

        Statement* stmt_ = nullptr;
    };

    TypedRowRange() noexcept = default;
    explicit TypedRowRange(Statement& stmt) noexcept : stmt_(&stmt) {
    }

    Iterator begin() {
        if (stmt_ != nullptr && stmt_->step()) {
            return Iterator(stmt_);
        }
        return Iterator(nullptr);
    }

    std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

    private:
    Statement* stmt_ = nullptr;
};

// ============================================================================
// Template & inline implementations
// ============================================================================

template <Bindable... Args>
Statement& Statement::bind_all(Args&&... args) {
    int index = 1;
    (bind(index++, std::forward<Args>(args)), ...);
    return *this;
}

inline RowRange Statement::rows() noexcept {
    return RowRange(*this);
}

template <ColumnValue... Ts>
    requires(sizeof...(Ts) >= 1)
inline TypedRowRange<Ts...> Statement::rows() noexcept {
    return TypedRowRange<Ts...>(*this);
}

namespace detail {

template <typename T>
struct OptionalInner {
    static constexpr bool is_optional = false;
    using type = T;
};

template <typename U>
struct OptionalInner<std::optional<U>> {
    static constexpr bool is_optional = true;
    using type = U;
};

} // namespace detail

template <ColumnValue T>
T Statement::column_as(int index) const {
    using Decayed = std::remove_cvref_t<T>;
    if constexpr (detail::OptionalInner<Decayed>::is_optional) {
        using Inner = typename detail::OptionalInner<Decayed>::type;
        if (column_is_null(index)) {
            return std::nullopt;
        }
        return column_as<Inner>(index);
    } else if constexpr (std::same_as<Decayed, bool>) {
        return column_int64(index) != 0;
    } else if constexpr (std::same_as<Decayed, int32_t>) {
        return column_int(index);
    } else if constexpr (std::same_as<Decayed, int64_t>) {
        return column_int64(index);
    } else if constexpr (std::same_as<Decayed, double>) {
        return column_double(index);
    } else if constexpr (std::same_as<Decayed, std::string>) {
        return std::string(column_text(index));
    } else if constexpr (std::same_as<Decayed, std::string_view>) {
        return column_text(index);
    } else if constexpr (std::same_as<Decayed, std::span<const std::byte>>) {
        return column_blob(index);
    } else if constexpr (std::same_as<Decayed, std::vector<std::byte>>) {
        auto b = column_blob(index);
        return std::vector<std::byte>(b.begin(), b.end());
    } else if constexpr (std::same_as<Decayed, std::vector<uint8_t>>) {
        auto b = column_blob(index);
        const auto* p = reinterpret_cast<const uint8_t*>(b.data());
        return std::vector<uint8_t>(p, p + b.size());
    } else if constexpr (std::same_as<Decayed, Value>) {
        return column_value(index);
    }
}

static_assert(std::ranges::input_range<RowRange>);
static_assert(std::ranges::view<RowRange>);
static_assert(std::ranges::input_range<TypedRowRange<int64_t>>);
static_assert(std::ranges::view<TypedRowRange<int64_t, std::string>>);

} // namespace sqlitex

#pragma once

#include "sqlitex/backup.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"
#include "sqlitex/sqlite.h"
#include "sqlitex/value.h"

#include <format>
#include <string_view>

namespace sqlitex {

[[nodiscard]] constexpr std::string_view to_string_view(
    ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Ok:
        return "Ok";
    case ErrorCode::Error:
        return "Error";
    case ErrorCode::Internal:
        return "Internal";
    case ErrorCode::Perm:
        return "Perm";
    case ErrorCode::Abort:
        return "Abort";
    case ErrorCode::Busy:
        return "Busy";
    case ErrorCode::Locked:
        return "Locked";
    case ErrorCode::NoMem:
        return "NoMem";
    case ErrorCode::ReadOnly:
        return "ReadOnly";
    case ErrorCode::Interrupt:
        return "Interrupt";
    case ErrorCode::IoErr:
        return "IoErr";
    case ErrorCode::Corrupt:
        return "Corrupt";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::Full:
        return "Full";
    case ErrorCode::CantOpen:
        return "CantOpen";
    case ErrorCode::Protocol:
        return "Protocol";
    case ErrorCode::Empty:
        return "Empty";
    case ErrorCode::Schema:
        return "Schema";
    case ErrorCode::TooBig:
        return "TooBig";
    case ErrorCode::Constraint:
        return "Constraint";
    case ErrorCode::Mismatch:
        return "Mismatch";
    case ErrorCode::Misuse:
        return "Misuse";
    case ErrorCode::NoLfs:
        return "NoLfs";
    case ErrorCode::Auth:
        return "Auth";
    case ErrorCode::Format:
        return "Format";
    case ErrorCode::Range:
        return "Range";
    case ErrorCode::NotADb:
        return "NotADb";
    case ErrorCode::Notice:
        return "Notice";
    case ErrorCode::Warning:
        return "Warning";
    case ErrorCode::Row:
        return "Row";
    case ErrorCode::Done:
        return "Done";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string_view(
    Value::Type type) noexcept {
    switch (type) {
    case Value::Type::Integer:
        return "INTEGER";
    case Value::Type::Float:
        return "FLOAT";
    case Value::Type::Text:
        return "TEXT";
    case Value::Type::Blob:
        return "BLOB";
    case Value::Type::Null:
        return "NULL";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string_view(
    ChangeOperation op) noexcept {
    switch (op) {
    case ChangeOperation::Insert:
        return "INSERT";
    case ChangeOperation::Delete:
        return "DELETE";
    case ChangeOperation::Update:
        return "UPDATE";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string_view(
    TxnState state) noexcept {
    switch (state) {
    case TxnState::None:
        return "None";
    case TxnState::Read:
        return "Read";
    case TxnState::Write:
        return "Write";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string_view(
    ThreadingMode mode) noexcept {
    switch (mode) {
    case ThreadingMode::SingleThread:
        return "SingleThread";
    case ThreadingMode::Serialized:
        return "Serialized";
    case ThreadingMode::MultiThread:
        return "MultiThread";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string_view(
    BackupStepResult result) noexcept {
    switch (result) {
    case BackupStepResult::More:
        return "More";
    case BackupStepResult::Done:
        return "Done";
    case BackupStepResult::Busy:
        return "Busy";
    case BackupStepResult::Locked:
        return "Locked";
    }
    return "Unknown";
}

} // namespace sqlitex

template <>
struct std::formatter<sqlitex::ErrorCode> : std::formatter<std::string_view> {
    auto format(sqlitex::ErrorCode code, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(
            sqlitex::to_string_view(code), ctx);
    }
};

template <>
struct std::formatter<sqlitex::Value::Type> : std::formatter<std::string_view> {
    auto format(sqlitex::Value::Type type, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(
            sqlitex::to_string_view(type), ctx);
    }
};

template <>
struct std::formatter<sqlitex::ChangeOperation>
    : std::formatter<std::string_view> {
    auto format(sqlitex::ChangeOperation op, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(
            sqlitex::to_string_view(op), ctx);
    }
};

template <>
struct std::formatter<sqlitex::TxnState> : std::formatter<std::string_view> {
    auto format(sqlitex::TxnState state, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(
            sqlitex::to_string_view(state), ctx);
    }
};

template <>
struct std::formatter<sqlitex::ThreadingMode>
    : std::formatter<std::string_view> {
    auto format(sqlitex::ThreadingMode mode, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(
            sqlitex::to_string_view(mode), ctx);
    }
};

template <>
struct std::formatter<sqlitex::BackupStepResult>
    : std::formatter<std::string_view> {
    auto format(sqlitex::BackupStepResult result,
                std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(
            sqlitex::to_string_view(result), ctx);
    }
};

template <>
struct std::formatter<sqlitex::Value> {
    constexpr auto parse(std::format_parse_context& ctx) {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("invalid format args for sqlitex::Value");
        }
        return it;
    }

    auto format(const sqlitex::Value& value, std::format_context& ctx) const {
        switch (value.type()) {
        case sqlitex::Value::Type::Null:
            return std::format_to(ctx.out(), "NULL");
        case sqlitex::Value::Type::Integer:
            return std::format_to(ctx.out(), "{}", value.get_int());
        case sqlitex::Value::Type::Float:
            return std::format_to(ctx.out(), "{}", value.get_double());
        case sqlitex::Value::Type::Text:
            return std::format_to(ctx.out(), "{}", value.get_text());
        case sqlitex::Value::Type::Blob:
            return std::format_to(ctx.out(), "<blob {} bytes>",
                                  value.as_bytes().size());
        }
        return std::format_to(ctx.out(), "NULL");
    }
};

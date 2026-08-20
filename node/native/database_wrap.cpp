#include "database_wrap.h"

namespace sqlitex_node {

Napi::FunctionReference& DatabaseWrap::constructor() {
    static thread_local Napi::FunctionReference ref;
    return ref;
}

Napi::Object DatabaseWrap::Init(Napi::Env env, Napi::Object exports) {
    auto func = DefineClass(
        env, "NativeDatabase",
        {
            InstanceMethod("open", &DatabaseWrap::Open),
            InstanceMethod("close", &DatabaseWrap::Close),
            InstanceMethod("execute", &DatabaseWrap::Execute),
            InstanceMethod("query", &DatabaseWrap::Query),
            InstanceMethod("queryValue", &DatabaseWrap::QueryValue),
            InstanceMethod("lastInsertRowid", &DatabaseWrap::LastInsertRowid),
            InstanceMethod("changes", &DatabaseWrap::Changes),
            InstanceMethod("interrupt", &DatabaseWrap::Interrupt),
            InstanceMethod("setBusyTimeout", &DatabaseWrap::SetBusyTimeout),
            InstanceAccessor("isOpen", &DatabaseWrap::IsOpen, nullptr),
        });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("NativeDatabase", func);
    return exports;
}

DatabaseWrap::DatabaseWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<DatabaseWrap>(info) {
}

DatabaseWrap::~DatabaseWrap() {
    try {
        db_.close();
    } catch (...) {
    }
}

Napi::Value DatabaseWrap::Open(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "open(path, options?) requires a path")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string path = info[0].As<Napi::String>().Utf8Value();
    int flags = static_cast<int>(sqlitex::OpenFlag::ReadWrite) |
                static_cast<int>(sqlitex::OpenFlag::Create);
    std::string vfs;
    Napi::Object bcvfs_obj;
    bool have_bcvfs = false;

    if (info.Length() >= 2 && info[1].IsObject()) {
        auto opts = info[1].As<Napi::Object>();
        if (opts.Has("flags") && opts.Get("flags").IsNumber()) {
            flags = opts.Get("flags").As<Napi::Number>().Int32Value();
        }
        if (opts.Has("vfs") && opts.Get("vfs").IsString()) {
            vfs = opts.Get("vfs").As<Napi::String>().Utf8Value();
        }
        if (opts.Has("bcvfs") && opts.Get("bcvfs").IsObject()) {
            bcvfs_obj = opts.Get("bcvfs").As<Napi::Object>();
            have_bcvfs = true;
            if (bcvfs_obj.Has("vfsName")) {
                auto name = bcvfs_obj.Get("vfsName");
                if (name.IsString()) {
                    vfs = name.As<Napi::String>().Utf8Value();
                }
            }
        }
    }

    if (have_bcvfs) {
        set_bcvfs_ref(bcvfs_obj);
    }

    return queue_work(
        env, info.This().As<Napi::Object>(), gate_,
        [this, path, flags, vfs]() { db_.open(path, flags, vfs); });
}

Napi::Value DatabaseWrap::Close(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    throw_if_reentrant(env, "Database.close");
    if (dependents_ > 0) {
        Napi::Error::New(env, "cannot close Database while Session dependents "
                              "are still alive")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (gate_.has_pending()) {
        Napi::Error::New(env, "cannot close Database while operations are in "
                              "flight")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    gate_.mark_closing();
    db_.close();
    bcvfs_ref_.Reset();
    return env.Undefined();
}

Napi::Value DatabaseWrap::Execute(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "execute(sql) requires a string")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string sql = info[0].As<Napi::String>().Utf8Value();
    return queue_work(env, info.This().As<Napi::Object>(), gate_,
                      [this, sql]() { db_.execute(sql); });
}

Napi::Value DatabaseWrap::Query(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "query(sql) requires a string")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string sql = info[0].As<Napi::String>().Utf8Value();
    auto columns = std::make_shared<std::vector<std::string>>();
    auto rows = std::make_shared<std::vector<std::vector<sqlitex::Value>>>();

    return queue_work(
        env, info.This().As<Napi::Object>(), gate_,
        [this, sql, columns, rows]() {
            auto stmt = db_.prepare(sql);
            const int ncols = stmt.column_count();
            columns->reserve(static_cast<size_t>(ncols));
            for (int i = 0; i < ncols; ++i) {
                columns->emplace_back(stmt.column_name(i));
            }
            while (stmt.step()) {
                std::vector<sqlitex::Value> row;
                row.reserve(static_cast<size_t>(ncols));
                for (int i = 0; i < ncols; ++i) {
                    row.push_back(stmt.column_value(i));
                }
                rows->push_back(std::move(row));
            }
        },
        [columns, rows](Napi::Env env) -> Napi::Value {
            auto result = Napi::Object::New(env);
            auto col_arr = Napi::Array::New(env, columns->size());
            for (size_t i = 0; i < columns->size(); ++i) {
                col_arr.Set(static_cast<uint32_t>(i),
                            Napi::String::New(env, (*columns)[i]));
            }
            auto row_arr = Napi::Array::New(env, rows->size());
            for (size_t r = 0; r < rows->size(); ++r) {
                auto rec = Napi::Object::New(env);
                for (size_t c = 0; c < columns->size() && c < (*rows)[r].size();
                     ++c) {
                    rec.Set((*columns)[c], value_to_js(env, (*rows)[r][c]));
                }
                row_arr.Set(static_cast<uint32_t>(r), rec);
            }
            result.Set("columns", col_arr);
            result.Set("rows", row_arr);
            return result;
        });
}

Napi::Value DatabaseWrap::QueryValue(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "queryValue(sql) requires a string")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string sql = info[0].As<Napi::String>().Utf8Value();
    auto value = std::make_shared<std::optional<sqlitex::Value>>();
    auto found = std::make_shared<bool>(false);

    return queue_work(
        env, info.This().As<Napi::Object>(), gate_,
        [this, sql, value, found]() {
            auto stmt = db_.prepare(sql);
            if (stmt.step() && stmt.column_count() > 0) {
                *value = stmt.column_value(0);
                *found = true;
            }
        },
        [value, found](Napi::Env env) -> Napi::Value {
            if (!*found || !value->has_value()) {
                return env.Null();
            }
            return value_to_js(env, **value);
        });
}

// Synchronous accessors touch the connection on the JS thread, so they must
// not run while a worker thread is inside sqlite3_step on the same handle.
static bool require_idle(SerialGate& gate, Napi::Env env, const char* what) {
    if (gate.has_pending()) {
        Napi::Error::New(env, std::string(what) +
                                  " cannot run while other database "
                                  "operations are in flight")
            .ThrowAsJavaScriptException();
        return false;
    }
    return true;
}

Napi::Value DatabaseWrap::LastInsertRowid(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_idle(gate_, env, "lastInsertRowid")) {
        return env.Undefined();
    }
    auto rowid = db_.last_insert_rowid();
    constexpr int64_t kMax = 9007199254740991LL;
    if (rowid > kMax || rowid < -kMax) {
        return Napi::BigInt::New(env, rowid);
    }
    return Napi::Number::New(env, static_cast<double>(rowid));
}

Napi::Value DatabaseWrap::Changes(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_idle(gate_, env, "changes")) {
        return env.Undefined();
    }
    return Napi::Number::New(env, db_.changes());
}

Napi::Value DatabaseWrap::Interrupt(const Napi::CallbackInfo& info) {
    db_.interrupt();
    return info.Env().Undefined();
}

Napi::Value DatabaseWrap::SetBusyTimeout(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "setBusyTimeout(ms) requires a number")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!require_idle(gate_, env, "setBusyTimeout")) {
        return env.Undefined();
    }
    db_.set_busy_timeout(info[0].As<Napi::Number>().Int32Value());
    return env.Undefined();
}

Napi::Value DatabaseWrap::IsOpen(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), static_cast<bool>(db_));
}

} // namespace sqlitex_node

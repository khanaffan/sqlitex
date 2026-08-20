#include "session_wrap.h"

#ifdef SQLITEX_HAS_SESSION

#include "changeset_wrap.h"

namespace sqlitex_node {

Napi::FunctionReference& SessionWrap::constructor() {
    static thread_local Napi::FunctionReference ref;
    return ref;
}

Napi::Object SessionWrap::Init(Napi::Env env, Napi::Object exports) {
    auto func = DefineClass(
        env, "NativeSession",
        {
            InstanceMethod("enable", &SessionWrap::Enable),
            InstanceMethod("setIndirect", &SessionWrap::SetIndirect),
            InstanceMethod("attach", &SessionWrap::Attach),
            InstanceMethod("attachAll", &SessionWrap::AttachAll),
            InstanceMethod("setTableFilter", &SessionWrap::SetTableFilter),
            InstanceMethod("changeset", &SessionWrap::Changeset),
            InstanceMethod("patchset", &SessionWrap::Patchset),
            InstanceMethod("diff", &SessionWrap::Diff),
            InstanceAccessor("isEmpty", &SessionWrap::IsEmpty, nullptr),
            InstanceAccessor("memoryUsed", &SessionWrap::MemoryUsed, nullptr),
            InstanceMethod("close", &SessionWrap::Close),
        });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("NativeSession", func);
    return exports;
}

SessionWrap::SessionWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<SessionWrap>(info) {
    auto env = info.Env();
    try {
        dbw_ = unwrap_checked<DatabaseWrap>(info[0], "Database");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return;
    }
    auto db_obj = info[0].As<Napi::Object>();
    db_ref_ = Napi::Persistent(db_obj);
    dbw_->add_dependent();

    std::string db_name = "main";
    if (info.Length() >= 2 && info[1].IsString()) {
        db_name = info[1].As<Napi::String>().Utf8Value();
    }
    try {
        session_ = std::make_unique<sqlitex::Session>(dbw_->db(), db_name);
    } catch (const sqlitex::Error& e) {
        dbw_->remove_dependent();
        db_ref_.Reset();
        throw_js(env, e);
    }
}

SessionWrap::~SessionWrap() {
    if (session_) {
        try {
            session_->set_table_filter({});
        } catch (...) {
        }
    }
    session_.reset();
    table_filter_.reset();
    if (dbw_ != nullptr) {
        dbw_->remove_dependent();
        dbw_ = nullptr;
    }
    db_ref_.Reset();
}

bool SessionWrap::db_busy() const {
    return dbw_ != nullptr && dbw_->gate().has_pending();
}

static bool require_open(SessionWrap* self, Napi::Env env) {
    if (self == nullptr || !self->alive()) {
        Napi::Error::New(env, "Session is closed").ThrowAsJavaScriptException();
        return false;
    }
    // A table filter runs on a worker thread inside SQLite, where an exception
    // cannot propagate. Surface it on the next session call.
    if (self->db_busy()) {
        Napi::Error::New(env, "session operations cannot run while other "
                              "database operations are in flight")
            .ThrowAsJavaScriptException();
        return false;
    }
    auto pending = self->filter_error().take();
    if (!pending.empty()) {
        Napi::Error::New(env, "table filter callback failed: " + pending)
            .ThrowAsJavaScriptException();
        return false;
    }
    return true;
}

Napi::Value SessionWrap::Enable(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    bool enabled = true;
    if (info.Length() >= 1) {
        enabled = info[0].ToBoolean();
    }
    try {
        return Napi::Boolean::New(env, session_->enable(enabled));
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

Napi::Value SessionWrap::SetIndirect(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    bool indirect = true;
    if (info.Length() >= 1) {
        indirect = info[0].ToBoolean();
    }
    try {
        return Napi::Boolean::New(env, session_->set_indirect(indirect));
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

Napi::Value SessionWrap::Attach(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    std::string table;
    if (info.Length() >= 1 && info[0].IsString()) {
        table = info[0].As<Napi::String>().Utf8Value();
    }
    try {
        session_->attach(table);
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
    }
    return env.Undefined();
}

Napi::Value SessionWrap::AttachAll(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    try {
        session_->attach_all();
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
    }
    return env.Undefined();
}

Napi::Value SessionWrap::SetTableFilter(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    throw_if_reentrant(env, "Session.setTableFilter");
    if (info.Length() < 1 || !info[0].IsFunction()) {
        session_->set_table_filter({});
        table_filter_.reset();
        return env.Undefined();
    }
    table_filter_.set(env, info[0].As<Napi::Function>(), "session-table-filter");
    session_->set_table_filter([this](std::string_view table) -> bool {
        try {
            std::string name(table);
            return table_filter_.blocking<bool>(
                [name](Napi::Env env, Napi::Function js) {
                    auto ret = js.Call({Napi::String::New(env, name)});
                    if (ret.IsPromise()) {
                        throw Napi::Error::New(
                            env, "table filter must be synchronous (must not "
                                 "return a Promise)");
                    }
                    return ret.ToBoolean().Value();
                },
                false);
        } catch (const std::exception& e) {
            filter_error_.stash(e.what());
            return false;
        } catch (...) {
            filter_error_.stash("table filter callback failed");
            return false;
        }
    });
    return env.Undefined();
}

Napi::Value SessionWrap::Changeset(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    try {
        return ChangesetWrap::from_native(env, session_->changeset());
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

Napi::Value SessionWrap::Patchset(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    try {
        return PatchsetWrap::from_native(env, session_->patchset());
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

Napi::Value SessionWrap::Diff(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "diff(table, fromDb?) requires a table name")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string table = info[0].As<Napi::String>().Utf8Value();
    std::string from_db = "main";
    if (info.Length() >= 2 && info[1].IsString()) {
        from_db = info[1].As<Napi::String>().Utf8Value();
    }
    try {
        return ChangesetWrap::from_native(env, session_->diff(table, from_db));
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

Napi::Value SessionWrap::IsEmpty(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    return Napi::Boolean::New(env, session_->is_empty());
}

Napi::Value SessionWrap::MemoryUsed(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!require_open(this, env)) {
        return env.Undefined();
    }
    return Napi::Number::New(env, static_cast<double>(session_->memory_used()));
}

Napi::Value SessionWrap::Close(const Napi::CallbackInfo& info) {
    if (session_) {
        try {
            session_->set_table_filter({});
        } catch (...) {
        }
    }
    session_.reset();
    table_filter_.reset();
    if (dbw_ != nullptr) {
        dbw_->remove_dependent();
        dbw_ = nullptr;
    }
    db_ref_.Reset();
    return info.Env().Undefined();
}

} // namespace sqlitex_node

#endif

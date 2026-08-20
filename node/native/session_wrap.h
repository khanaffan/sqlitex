#pragma once

#include "common.h"

#ifdef SQLITEX_HAS_SESSION

#include "database_wrap.h"

namespace sqlitex_node {

class SessionWrap : public Napi::ObjectWrap<SessionWrap> {
    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    // Per-thread (hence per-napi_env) so loading the addon into a
    // worker_thread cannot clobber the main thread's constructor.
    static Napi::FunctionReference& constructor();

    explicit SessionWrap(const Napi::CallbackInfo& info);
    ~SessionWrap() override;

    sqlitex::Session& session() {
        return *session_;
    }

    bool alive() const noexcept {
        return static_cast<bool>(session_);
    }

    bool db_busy() const;

    CallbackError& filter_error() {
        return filter_error_;
    }

    private:
    Napi::Value Enable(const Napi::CallbackInfo& info);
    Napi::Value SetIndirect(const Napi::CallbackInfo& info);
    Napi::Value Attach(const Napi::CallbackInfo& info);
    Napi::Value AttachAll(const Napi::CallbackInfo& info);
    Napi::Value SetTableFilter(const Napi::CallbackInfo& info);
    Napi::Value Changeset(const Napi::CallbackInfo& info);
    Napi::Value Patchset(const Napi::CallbackInfo& info);
    Napi::Value Diff(const Napi::CallbackInfo& info);
    Napi::Value IsEmpty(const Napi::CallbackInfo& info);
    Napi::Value MemoryUsed(const Napi::CallbackInfo& info);
    Napi::Value Close(const Napi::CallbackInfo& info);

    std::unique_ptr<sqlitex::Session> session_{};
    Napi::ObjectReference db_ref_{};
    DatabaseWrap* dbw_ = nullptr;
    CallbackError filter_error_{};
    JsCallback table_filter_{};
};

} // namespace sqlitex_node

#endif

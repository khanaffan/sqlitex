#pragma once

#include "common.h"

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex_node {

class ChangesetWrap : public Napi::ObjectWrap<ChangesetWrap> {
    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    // Per-thread (hence per-napi_env) so loading the addon into a
    // worker_thread cannot clobber the main thread's constructor.
    static Napi::FunctionReference& constructor();
    static Napi::Object from_native(Napi::Env env, sqlitex::Changeset cs);

    explicit ChangesetWrap(const Napi::CallbackInfo& info);

    sqlitex::Changeset& changeset() {
        return cs_;
    }

    void take(sqlitex::Changeset cs) {
        cs_ = std::move(cs);
    }

    private:
    Napi::Value Size(const Napi::CallbackInfo& info);
    Napi::Value Empty(const Napi::CallbackInfo& info);
    Napi::Value ToBuffer(const Napi::CallbackInfo& info);
    static Napi::Value FromBuffer(const Napi::CallbackInfo& info);
    Napi::Value Concat(const Napi::CallbackInfo& info);
    Napi::Value Invert(const Napi::CallbackInfo& info);
    Napi::Value Apply(const Napi::CallbackInfo& info);

    sqlitex::Changeset cs_{};
};

class PatchsetWrap : public Napi::ObjectWrap<PatchsetWrap> {
    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    // Per-thread (hence per-napi_env) so loading the addon into a
    // worker_thread cannot clobber the main thread's constructor.
    static Napi::FunctionReference& constructor();
    static Napi::Object from_native(Napi::Env env, sqlitex::Patchset ps);

    explicit PatchsetWrap(const Napi::CallbackInfo& info);

    sqlitex::Patchset& patchset() {
        return ps_;
    }

    void take(sqlitex::Patchset ps) {
        ps_ = std::move(ps);
    }

    private:
    Napi::Value Size(const Napi::CallbackInfo& info);
    Napi::Value Empty(const Napi::CallbackInfo& info);
    Napi::Value ToBuffer(const Napi::CallbackInfo& info);
    Napi::Value Apply(const Napi::CallbackInfo& info);

    sqlitex::Patchset ps_{};
};

} // namespace sqlitex_node

#endif

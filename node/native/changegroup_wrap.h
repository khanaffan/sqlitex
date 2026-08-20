#pragma once

#include "common.h"

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex_node {

class ChangegroupWrap : public Napi::ObjectWrap<ChangegroupWrap> {
    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    // Per-thread (hence per-napi_env) so loading the addon into a
    // worker_thread cannot clobber the main thread's constructor.
    static Napi::FunctionReference& constructor();

    explicit ChangegroupWrap(const Napi::CallbackInfo& info);

    private:
    Napi::Value Add(const Napi::CallbackInfo& info);
    Napi::Value Output(const Napi::CallbackInfo& info);

    sqlitex::Changegroup group_{};
};

} // namespace sqlitex_node

#endif

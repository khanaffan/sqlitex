#pragma once

#include "common.h"

#ifdef SQLITEX_HAS_BCVFS

namespace sqlitex_node {

class BcvContainerWrap : public Napi::ObjectWrap<BcvContainerWrap> {
    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    // Per-thread (hence per-napi_env) so loading the addon into a
    // worker_thread cannot clobber the main thread's constructor.
    static Napi::FunctionReference& constructor();

    explicit BcvContainerWrap(const Napi::CallbackInfo& info);

    private:
    static Napi::Value Create(const Napi::CallbackInfo& info);
    static Napi::Value Destroy(const Napi::CallbackInfo& info);
    static Napi::Value Upload(const Napi::CallbackInfo& info);
    static Napi::Value Download(const Napi::CallbackInfo& info);
    static Napi::Value Delete(const Napi::CallbackInfo& info);
};

} // namespace sqlitex_node

#endif

#pragma once

#include "common.h"

#ifdef SQLITEX_HAS_BCVFS

namespace sqlitex_node {

class BcvfsWrap : public Napi::ObjectWrap<BcvfsWrap> {
    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    // Per-thread (hence per-napi_env) so loading the addon into a
    // worker_thread cannot clobber the main thread's constructor.
    static Napi::FunctionReference& constructor();

    explicit BcvfsWrap(const Napi::CallbackInfo& info);
    ~BcvfsWrap() override;

    sqlitex::Bcvfs& bcvfs() {
        return *bcvfs_;
    }

    SerialGate& gate() {
        return gate_;
    }

    const std::string& vfs_name() const {
        return bcvfs_->vfs_name();
    }

    private:
    // Wraps native work so an exception raised inside a JS auth callback (which
    // cannot propagate through the C shim) is reported instead of surfacing as
    // an opaque SQLITE_AUTH/CANTOPEN.
    void run_guarded(const std::function<void()>& fn);

    Napi::Value VfsName(const Napi::CallbackInfo& info);
    Napi::Value IsDaemon(const Napi::CallbackInfo& info);
    Napi::Value SetAuthCallback(const Napi::CallbackInfo& info);
    Napi::Value SetLogCallback(const Napi::CallbackInfo& info);
    Napi::Value Attach(const Napi::CallbackInfo& info);
    Napi::Value Detach(const Napi::CallbackInfo& info);
    Napi::Value Upload(const Napi::CallbackInfo& info);
    Napi::Value Poll(const Napi::CallbackInfo& info);
    Napi::Value Copy(const Napi::CallbackInfo& info);
    Napi::Value Remove(const Napi::CallbackInfo& info);
    Napi::Value Revert(const Napi::CallbackInfo& info);
    Napi::Value RegisterVtab(const Napi::CallbackInfo& info);

    std::unique_ptr<sqlitex::Bcvfs> bcvfs_{};
    SerialGate gate_{};
    JsCallback auth_{};
    JsCallback log_{};
    JsCallback busy_{};
    CallbackError cb_error_{};
};

} // namespace sqlitex_node

#endif

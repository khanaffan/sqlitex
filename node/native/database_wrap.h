#pragma once

#include "common.h"

namespace sqlitex_node {

class DatabaseWrap : public Napi::ObjectWrap<DatabaseWrap> {
    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    // Per-thread (hence per-napi_env) so loading the addon into a
    // worker_thread cannot clobber the main thread's constructor.
    static Napi::FunctionReference& constructor();

    explicit DatabaseWrap(const Napi::CallbackInfo& info);
    ~DatabaseWrap() override;

    sqlitex::Database& db() {
        return db_;
    }

    const SerialGate& gate() const {
        return gate_;
    }

    SerialGate& gate() {
        return gate_;
    }

    void add_dependent() {
        ++dependents_;
    }
    void remove_dependent() {
        if (dependents_ > 0) {
            --dependents_;
        }
    }
    int dependents() const {
        return dependents_;
    }

    void set_bcvfs_ref(Napi::Object obj) {
        bcvfs_ref_ = Napi::Persistent(obj);
    }

    private:
    Napi::Value Open(const Napi::CallbackInfo& info);
    Napi::Value Close(const Napi::CallbackInfo& info);
    Napi::Value Execute(const Napi::CallbackInfo& info);
    Napi::Value Query(const Napi::CallbackInfo& info);
    Napi::Value QueryValue(const Napi::CallbackInfo& info);
    Napi::Value LastInsertRowid(const Napi::CallbackInfo& info);
    Napi::Value Changes(const Napi::CallbackInfo& info);
    Napi::Value Interrupt(const Napi::CallbackInfo& info);
    Napi::Value SetBusyTimeout(const Napi::CallbackInfo& info);
    Napi::Value IsOpen(const Napi::CallbackInfo& info);

    sqlitex::Database db_{};
    SerialGate gate_{};
    Napi::ObjectReference bcvfs_ref_{};
    int dependents_ = 0;
};

} // namespace sqlitex_node

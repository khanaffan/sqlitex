#include "container_wrap.h"

#ifdef SQLITEX_HAS_BCVFS

#include <bcvutil.h>

namespace sqlitex_node {

namespace {

struct ContainerArgs {
    std::string storage;
    std::string account;
    std::string auth;
    std::string container;
    int name_size = 16;
    int block_size = 4194304;
};

ContainerArgs parse_args(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        throw std::invalid_argument(
            "BcvContainer.create/destroy requires an options object");
    }
    auto opts = info[0].As<Napi::Object>();
    ContainerArgs a;
    a.storage = require_string(opts.Get("storage"), "storage");
    a.account = require_string(opts.Get("account"), "account");
    a.auth = require_string(opts.Get("auth"), "auth");
    a.container = require_string(opts.Get("container"), "container");
    if (opts.Has("nameSize") && opts.Get("nameSize").IsNumber()) {
        a.name_size = opts.Get("nameSize").As<Napi::Number>().Int32Value();
    }
    if (opts.Has("blockSize") && opts.Get("blockSize").IsNumber()) {
        a.block_size = opts.Get("blockSize").As<Napi::Number>().Int32Value();
    }
    (void)env;
    return a;
}

void run_create(const ContainerArgs& a) {
    sqlite3_bcv* bcv = nullptr;
    int rc = sqlite3_bcv_open(a.storage.c_str(), a.account.c_str(),
                              a.auth.c_str(), a.container.c_str(), &bcv);
    if (bcv == nullptr) {
        throw std::runtime_error("sqlite3_bcv_open returned null");
    }
    if (rc != 0) {
        std::string msg = sqlite3_bcv_errmsg(bcv)
                              ? sqlite3_bcv_errmsg(bcv)
                              : "sqlite3_bcv_open failed";
        sqlite3_bcv_close(bcv);
        throw sqlitex::Error(rc, msg);
    }
    rc = sqlite3_bcv_create(bcv, a.name_size, a.block_size);
    if (rc != 0) {
        std::string msg = sqlite3_bcv_errmsg(bcv)
                              ? sqlite3_bcv_errmsg(bcv)
                              : "sqlite3_bcv_create failed";
        sqlite3_bcv_close(bcv);
        throw sqlitex::Error(rc, msg);
    }
    sqlite3_bcv_close(bcv);
}

void run_destroy(const ContainerArgs& a) {
    sqlite3_bcv* bcv = nullptr;
    int rc = sqlite3_bcv_open(a.storage.c_str(), a.account.c_str(),
                              a.auth.c_str(), a.container.c_str(), &bcv);
    if (bcv == nullptr) {
        throw std::runtime_error("sqlite3_bcv_open returned null");
    }
    if (rc != 0) {
        std::string msg = sqlite3_bcv_errmsg(bcv)
                              ? sqlite3_bcv_errmsg(bcv)
                              : "sqlite3_bcv_open failed";
        sqlite3_bcv_close(bcv);
        throw sqlitex::Error(rc, msg);
    }
    rc = sqlite3_bcv_destroy(bcv);
    if (rc != 0) {
        std::string msg = sqlite3_bcv_errmsg(bcv)
                              ? sqlite3_bcv_errmsg(bcv)
                              : "sqlite3_bcv_destroy failed";
        sqlite3_bcv_close(bcv);
        throw sqlitex::Error(rc, msg);
    }
    sqlite3_bcv_close(bcv);
}

// Runs an operation against an opened container handle, mapping bcv error
// codes and messages onto sqlitex::Error.
void run_with_handle(const ContainerArgs& a, const char* what,
                     const std::function<int(sqlite3_bcv*)>& op) {
    sqlite3_bcv* bcv = nullptr;
    int rc = sqlite3_bcv_open(a.storage.c_str(), a.account.c_str(),
                              a.auth.c_str(), a.container.c_str(), &bcv);
    if (bcv == nullptr) {
        throw std::runtime_error("sqlite3_bcv_open returned null");
    }
    if (rc != 0) {
        std::string msg = sqlite3_bcv_errmsg(bcv)
                              ? sqlite3_bcv_errmsg(bcv)
                              : "sqlite3_bcv_open failed";
        sqlite3_bcv_close(bcv);
        throw sqlitex::Error(rc, msg);
    }
    rc = op(bcv);
    if (rc != 0) {
        std::string msg =
            sqlite3_bcv_errmsg(bcv) ? sqlite3_bcv_errmsg(bcv) : what;
        sqlite3_bcv_close(bcv);
        throw sqlitex::Error(rc, msg);
    }
    sqlite3_bcv_close(bcv);
}

class StaticPromiseWorker : public Napi::AsyncWorker {
    public:
    StaticPromiseWorker(Napi::Env env, std::function<void()> work)
        : Napi::AsyncWorker(env),
          deferred_(Napi::Promise::Deferred::New(env)), work_(std::move(work)) {
    }

    Napi::Promise Promise() {
        return deferred_.Promise();
    }

    void Execute() override {
        try {
            work_();
        } catch (const sqlitex::Error& e) {
            err_ = from_sqlitex(e);
        } catch (const std::exception& e) {
            err_ = from_exception(e);
        }
    }

    void OnOK() override {
        if (err_) {
            deferred_.Reject(make_js_error(Env(), *err_).Value());
        } else {
            deferred_.Resolve(Env().Undefined());
        }
    }

    void OnError(const Napi::Error& e) override {
        deferred_.Reject(e.Value());
    }

    private:
    Napi::Promise::Deferred deferred_;
    std::function<void()> work_{};
    std::optional<NativeError> err_{};
};

} // namespace

Napi::FunctionReference& BcvContainerWrap::constructor() {
    static thread_local Napi::FunctionReference ref;
    return ref;
}

Napi::Object BcvContainerWrap::Init(Napi::Env env, Napi::Object exports) {
    auto func = DefineClass(env, "NativeBcvContainer",
                            {
                                StaticMethod("create", &BcvContainerWrap::Create),
                                StaticMethod("destroy", &BcvContainerWrap::Destroy),
                                StaticMethod("upload", &BcvContainerWrap::Upload),
                                StaticMethod("download", &BcvContainerWrap::Download),
                                StaticMethod("delete", &BcvContainerWrap::Delete),
                            });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("NativeBcvContainer", func);
    return exports;
}

BcvContainerWrap::BcvContainerWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<BcvContainerWrap>(info) {
}

Napi::Value BcvContainerWrap::Create(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    try {
        auto args = parse_args(info);
        auto* worker = new StaticPromiseWorker(env, [args]() { run_create(args); });
        worker->Queue();
        return worker->Promise();
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value BcvContainerWrap::Destroy(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    try {
        auto args = parse_args(info);
        auto* worker =
            new StaticPromiseWorker(env, [args]() { run_destroy(args); });
        worker->Queue();
        return worker->Promise();
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value BcvContainerWrap::Upload(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    try {
        auto args = parse_args(info);
        auto local = require_string(info[1], "localPath");
        auto remote = require_string(info[2], "remoteName");
        auto* worker = new StaticPromiseWorker(env, [args, local, remote]() {
            run_with_handle(args, "sqlite3_bcv_upload failed",
                            [&](sqlite3_bcv* bcv) {
                                return sqlite3_bcv_upload(bcv, local.c_str(),
                                                          remote.c_str());
                            });
        });
        worker->Queue();
        return worker->Promise();
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value BcvContainerWrap::Download(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    try {
        auto args = parse_args(info);
        auto remote = require_string(info[1], "remoteName");
        auto local = require_string(info[2], "localPath");
        auto* worker = new StaticPromiseWorker(env, [args, remote, local]() {
            run_with_handle(args, "sqlite3_bcv_download failed",
                            [&](sqlite3_bcv* bcv) {
                                return sqlite3_bcv_download(bcv, remote.c_str(),
                                                            local.c_str());
                            });
        });
        worker->Queue();
        return worker->Promise();
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value BcvContainerWrap::Delete(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    try {
        auto args = parse_args(info);
        auto remote = require_string(info[1], "remoteName");
        auto* worker = new StaticPromiseWorker(env, [args, remote]() {
            run_with_handle(args, "sqlite3_bcv_delete failed",
                            [&](sqlite3_bcv* bcv) {
                                return sqlite3_bcv_delete(bcv, remote.c_str());
                            });
        });
        worker->Queue();
        return worker->Promise();
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

} // namespace sqlitex_node

#endif

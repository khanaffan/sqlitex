#include "bcvfs_wrap.h"

#ifdef SQLITEX_HAS_BCVFS

#include "database_wrap.h"

#include <chrono>

namespace sqlitex_node {

Napi::FunctionReference& BcvfsWrap::constructor() {
    static thread_local Napi::FunctionReference ref;
    return ref;
}

Napi::Object BcvfsWrap::Init(Napi::Env env, Napi::Object exports) {
    auto func = DefineClass(
        env, "NativeBcvfs",
        {
            InstanceAccessor("vfsName", &BcvfsWrap::VfsName, nullptr),
            InstanceAccessor("isDaemon", &BcvfsWrap::IsDaemon, nullptr),
            InstanceMethod("setAuthCallback", &BcvfsWrap::SetAuthCallback),
            InstanceMethod("setLogCallback", &BcvfsWrap::SetLogCallback),
            InstanceMethod("attach", &BcvfsWrap::Attach),
            InstanceMethod("detach", &BcvfsWrap::Detach),
            InstanceMethod("upload", &BcvfsWrap::Upload),
            InstanceMethod("poll", &BcvfsWrap::Poll),
            InstanceMethod("copy", &BcvfsWrap::Copy),
            InstanceMethod("remove", &BcvfsWrap::Remove),
            InstanceMethod("revert", &BcvfsWrap::Revert),
            InstanceMethod("registerVtab", &BcvfsWrap::RegisterVtab),
        });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("NativeBcvfs", func);
    return exports;
}

BcvfsWrap::BcvfsWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<BcvfsWrap>(info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Bcvfs requires an options object")
            .ThrowAsJavaScriptException();
        return;
    }
    auto opts = info[0].As<Napi::Object>();
    if (!opts.Has("cacheDir") || !opts.Get("cacheDir").IsString()) {
        Napi::TypeError::New(env, "Bcvfs options.cacheDir is required")
            .ThrowAsJavaScriptException();
        return;
    }
    std::string cache_dir = opts.Get("cacheDir").As<Napi::String>().Utf8Value();
    std::string vfs_name = "bcvfs";
    if (opts.Has("vfsName") && opts.Get("vfsName").IsString()) {
        vfs_name = opts.Get("vfsName").As<Napi::String>().Utf8Value();
    }

    try {
        bcvfs_ = std::make_unique<sqlitex::Bcvfs>(cache_dir, vfs_name);
        if (opts.Has("cacheSize") && opts.Get("cacheSize").IsNumber()) {
            bcvfs_->set_cache_size(
                opts.Get("cacheSize").As<Napi::Number>().Int64Value());
        }
        if (opts.Has("maxRequests") && opts.Get("maxRequests").IsNumber()) {
            bcvfs_->set_max_requests(
                opts.Get("maxRequests").As<Napi::Number>().Int32Value());
        }
        if (opts.Has("httpTimeoutSeconds") &&
            opts.Get("httpTimeoutSeconds").IsNumber()) {
            bcvfs_->set_http_timeout(std::chrono::seconds(
                opts.Get("httpTimeoutSeconds").As<Napi::Number>().Int32Value()));
        }
        if (opts.Has("httpLogging") && opts.Get("httpLogging").IsBoolean()) {
            bcvfs_->set_http_logging(
                opts.Get("httpLogging").As<Napi::Boolean>().Value());
        }
        // Auth/log callbacks are installed after construction from JS.
        // ThreadSafeFunction created here is not callable from workers.
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
    }
}

BcvfsWrap::~BcvfsWrap() {
    try {
        if (bcvfs_) {
            bcvfs_->set_auth_callback({});
            bcvfs_->set_log_callback({});
        }
    } catch (...) {
    }
    auth_.reset();
    log_.reset();
    busy_.reset();
    bcvfs_.reset();
}

Napi::Value BcvfsWrap::VfsName(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), bcvfs_->vfs_name());
}

Napi::Value BcvfsWrap::IsDaemon(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), bcvfs_->is_daemon());
}

Napi::Value BcvfsWrap::SetAuthCallback(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    throw_if_reentrant(env, "Bcvfs.setAuthCallback");
    Napi::Function fn;
    if (info.Length() >= 1 && info[0].IsString()) {
        const std::string token = info[0].As<Napi::String>().Utf8Value();
        auth_.reset();
        bcvfs_->set_auth_callback(
            [token](std::string_view, std::string_view, std::string_view,
                    std::string& out) -> bool {
                out = token;
                return true;
            });
        return env.Undefined();
    }
    if (info.Length() >= 1 && info[0].IsFunction()) {
        fn = info[0].As<Napi::Function>();
    } else if (info.Length() >= 1 && info[0].IsObject()) {
        auto opts = info[0].As<Napi::Object>();
        if (opts.Has("auth") && opts.Get("auth").IsString()) {
            const std::string token =
                opts.Get("auth").As<Napi::String>().Utf8Value();
            auth_.reset();
            bcvfs_->set_auth_callback(
                [token](std::string_view, std::string_view, std::string_view,
                        std::string& out) -> bool {
                    out = token;
                    return true;
                });
            return env.Undefined();
        }
        if (opts.Has("auth") && opts.Get("auth").IsFunction()) {
            fn = opts.Get("auth").As<Napi::Function>();
        }
    }
    if (fn.IsEmpty()) {
        bcvfs_->set_auth_callback({});
        auth_.reset();
        return env.Undefined();
    }
    auth_.set(env, fn, "bcvfs-auth");
    bcvfs_->set_auth_callback(
        [this](std::string_view storage, std::string_view account,
               std::string_view container, std::string& token) -> bool {
            try {
            std::string s(storage);
            std::string a(account);
            std::string c(container);
            auto result = auth_.blocking<std::pair<bool, std::string>>(
                [s, a, c](Napi::Env env, Napi::Function js) {
                    auto arg = Napi::Object::New(env);
                    arg.Set("storage", Napi::String::New(env, s));
                    arg.Set("account", Napi::String::New(env, a));
                    arg.Set("container", Napi::String::New(env, c));
                    auto ret = js.Call({arg});
                    if (ret.IsPromise()) {
                        throw Napi::Error::New(
                            env, "auth callback must be synchronous (must not "
                                 "return a Promise)");
                    }
                    if (ret.IsBoolean() && !ret.As<Napi::Boolean>().Value()) {
                        return std::make_pair(false, std::string());
                    }
                    if (ret.IsString()) {
                        return std::make_pair(true,
                                              ret.As<Napi::String>().Utf8Value());
                    }
                    if (ret.IsNull() || ret.IsUndefined()) {
                        return std::make_pair(false, std::string());
                    }
                    throw Napi::TypeError::New(
                        env, "auth callback must return a string token or false");
                },
                {false, std::string()});
            if (result.first) {
                token = std::move(result.second);
            }
            return result.first;
            } catch (const std::exception& e) {
                cb_error_.stash(e.what());
                return false;
            } catch (...) {
                cb_error_.stash("auth callback failed");
                return false;
            }
        });
    return env.Undefined();
}

Napi::Value BcvfsWrap::SetLogCallback(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsFunction()) {
        bcvfs_->set_log_callback({});
        log_.reset();
        return env.Undefined();
    }
    log_.set(env, info[0].As<Napi::Function>(), "bcvfs-log", 128);
    bcvfs_->set_log_callback([this](std::string_view message) {
        log_.nonblocking(std::string(message));
    });
    return env.Undefined();
}

Napi::Value BcvfsWrap::Attach(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "attach({storage, account, container, alias})")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto opts = info[0].As<Napi::Object>();
    std::string storage;
    std::string account;
    std::string container;
    std::string alias;
    try {
        storage = require_string(opts.Get("storage"), "storage");
        account = require_string(opts.Get("account"), "account");
        container = require_string(opts.Get("container"), "container");
        alias = require_string(opts.Get("alias"), "alias");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return queue_work(env, info.This().As<Napi::Object>(), gate_,
                      [this, storage, account, container, alias]() {
                          run_guarded([&] {
                              bcvfs_->attach(storage, account, container,
                                             alias);
                          });
                      });
}

Napi::Value BcvfsWrap::Detach(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "detach(alias) requires a string")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string alias = info[0].As<Napi::String>().Utf8Value();
    return queue_work(env, info.This().As<Napi::Object>(), gate_,
                      [this, alias]() {
                          run_guarded([&] { bcvfs_->detach(alias); });
                      });
}

Napi::Value BcvfsWrap::Upload(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "upload(container, options?) requires a string")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string container = info[0].As<Napi::String>().Utf8Value();
    auto uploaded = std::make_shared<int>(0);
    auto on_busy = std::make_shared<JsCallback>();
    if (info.Length() >= 2 && info[1].IsObject()) {
        auto opts = info[1].As<Napi::Object>();
        if (opts.Has("onBusy") && opts.Get("onBusy").IsFunction()) {
            on_busy->set(env, opts.Get("onBusy").As<Napi::Function>(),
                         "bcvfs-busy");
        }
    }
    return queue_work(
        env, info.This().As<Napi::Object>(), gate_,
        [this, container, uploaded, on_busy]() {
            sqlitex::BcvfsBusyCallback busy;
            if (on_busy->armed()) {
                busy = [on_busy](int retry)
                    -> std::optional<std::chrono::milliseconds> {
                    auto ms = on_busy->blocking<std::optional<int>>(
                        [retry](Napi::Env env, Napi::Function js) {
                            auto ret =
                                js.Call({Napi::Number::New(env, retry)});
                            if (ret.IsPromise()) {
                                throw Napi::Error::New(
                                    env, "busy callback must be synchronous");
                            }
                            if (ret.IsNull() || ret.IsUndefined() ||
                                (ret.IsBoolean() &&
                                 !ret.As<Napi::Boolean>().Value())) {
                                return std::optional<int>{};
                            }
                            if (ret.IsNumber()) {
                                return std::optional<int>{
                                    ret.As<Napi::Number>().Int32Value()};
                            }
                            return std::optional<int>{};
                        },
                        std::optional<int>{});
                    if (!ms) {
                        return std::nullopt;
                    }
                    return std::chrono::milliseconds(*ms);
                };
            }
            run_guarded([&] { *uploaded = bcvfs_->upload(container, busy); });
        },
        [uploaded](Napi::Env env) -> Napi::Value {
            return Napi::Number::New(env, *uploaded);
        });
}

Napi::Value BcvfsWrap::Poll(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "poll(container) requires a string")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string container = info[0].As<Napi::String>().Utf8Value();
    auto changed = std::make_shared<bool>(false);
    return queue_work(
        env, info.This().As<Napi::Object>(), gate_,
        [this, container, changed]() {
            run_guarded([&] { *changed = bcvfs_->poll(container); });
        },
        [changed](Napi::Env env) -> Napi::Value {
            return Napi::Boolean::New(env, *changed);
        });
}

Napi::Value BcvfsWrap::Copy(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "copy({container, from, to})")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto opts = info[0].As<Napi::Object>();
    std::string container;
    std::string from;
    std::string to;
    try {
        container = require_string(opts.Get("container"), "container");
        from = require_string(opts.Get("from"), "from");
        to = require_string(opts.Get("to"), "to");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return queue_work(env, info.This().As<Napi::Object>(), gate_,
                      [this, container, from, to]() {
                          run_guarded(
                              [&] { bcvfs_->copy(container, from, to); });
                      });
}

Napi::Value BcvfsWrap::Remove(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "remove({container, dbName})")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto opts = info[0].As<Napi::Object>();
    std::string container;
    std::string db_name;
    try {
        container = require_string(opts.Get("container"), "container");
        db_name = require_string(opts.Get("dbName"), "dbName");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return queue_work(env, info.This().As<Napi::Object>(), gate_,
                      [this, container, db_name]() {
                          run_guarded(
                              [&] { bcvfs_->remove(container, db_name); });
                      });
}

Napi::Value BcvfsWrap::Revert(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "revert(container) requires a string")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string container = info[0].As<Napi::String>().Utf8Value();
    return queue_work(env, info.This().As<Napi::Object>(), gate_,
                      [this, container]() {
                          run_guarded([&] { bcvfs_->revert(container); });
                      });
}

void BcvfsWrap::run_guarded(const std::function<void()>& fn) {
    cb_error_.take();
    try {
        fn();
    } catch (...) {
        auto pending = cb_error_.take();
        if (!pending.empty()) {
            throw std::runtime_error("auth callback failed: " + pending);
        }
        throw;
    }
    auto pending = cb_error_.take();
    if (!pending.empty()) {
        throw std::runtime_error("auth callback failed: " + pending);
    }
}

Napi::Value BcvfsWrap::RegisterVtab(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    DatabaseWrap* dbw = nullptr;
    try {
        dbw = unwrap_checked<DatabaseWrap>(info[0], "Database");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        bcvfs_->register_vtab(dbw->db());
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
    }
    return env.Undefined();
}

} // namespace sqlitex_node

#endif

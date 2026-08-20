#include "changeset_wrap.h"

#ifdef SQLITEX_HAS_SESSION

#include "database_wrap.h"

namespace sqlitex_node {

Napi::FunctionReference& ChangesetWrap::constructor() {
    static thread_local Napi::FunctionReference ref;
    return ref;
}
Napi::FunctionReference& PatchsetWrap::constructor() {
    static thread_local Napi::FunctionReference ref;
    return ref;
}

Napi::Object ChangesetWrap::Init(Napi::Env env, Napi::Object exports) {
    auto func = DefineClass(
        env, "NativeChangeset",
        {
            InstanceAccessor("size", &ChangesetWrap::Size, nullptr),
            InstanceAccessor("empty", &ChangesetWrap::Empty, nullptr),
            InstanceMethod("toBuffer", &ChangesetWrap::ToBuffer),
            StaticMethod("fromBuffer", &ChangesetWrap::FromBuffer),
            InstanceMethod("concat", &ChangesetWrap::Concat),
            InstanceMethod("invert", &ChangesetWrap::Invert),
            InstanceMethod("apply", &ChangesetWrap::Apply),
        });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("NativeChangeset", func);
    return exports;
}

ChangesetWrap::ChangesetWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ChangesetWrap>(info) {
}

Napi::Object ChangesetWrap::from_native(Napi::Env env, sqlitex::Changeset cs) {
    auto obj = constructor().New({});
    Unwrap(obj)->take(std::move(cs));
    return obj;
}

Napi::Value ChangesetWrap::Size(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), cs_.size());
}

Napi::Value ChangesetWrap::Empty(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), cs_.empty());
}

Napi::Value ChangesetWrap::ToBuffer(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (cs_.empty() || cs_.data() == nullptr) {
        return Napi::Buffer<uint8_t>::Copy(env, nullptr, 0);
    }
    return Napi::Buffer<uint8_t>::Copy(
        env, static_cast<const uint8_t*>(cs_.data()),
        static_cast<size_t>(cs_.size()));
}

Napi::Value ChangesetWrap::FromBuffer(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsBuffer()) {
        Napi::TypeError::New(env, "fromBuffer requires a Buffer")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto buf = info[0].As<Napi::Buffer<uint8_t>>();
    try {
        return from_native(env, sqlitex::Changeset::copy_from(
                                    buf.Data(), static_cast<int>(buf.Length())));
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

Napi::Value ChangesetWrap::Concat(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    ChangesetWrap* other = nullptr;
    try {
        other = unwrap_checked<ChangesetWrap>(info[0], "Changeset");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        return from_native(env, cs_.concat(other->cs_));
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

Napi::Value ChangesetWrap::Invert(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    try {
        return from_native(env, cs_.invert());
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

static sqlitex::ChangesetAction parse_action(const Napi::Value& v) {
    if (v.IsString()) {
        auto s = v.As<Napi::String>().Utf8Value();
        if (s == "replace") {
            return sqlitex::ChangesetAction::Replace;
        }
        if (s == "abort") {
            return sqlitex::ChangesetAction::Abort;
        }
        return sqlitex::ChangesetAction::Omit;
    }
    if (v.IsNumber()) {
        const int n = v.As<Napi::Number>().Int32Value();
        if (n == static_cast<int>(sqlitex::ChangesetAction::Replace)) {
            return sqlitex::ChangesetAction::Replace;
        }
        if (n == static_cast<int>(sqlitex::ChangesetAction::Abort)) {
            return sqlitex::ChangesetAction::Abort;
        }
    }
    return sqlitex::ChangesetAction::Omit;
}

Napi::Value ChangesetWrap::Apply(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    DatabaseWrap* dbw = nullptr;
    try {
        dbw = unwrap_checked<DatabaseWrap>(info[0], "Database");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto db_obj = info[0].As<Napi::Object>();

    int flags = 0;
    bool use_v3 = false;
    if (info.Length() >= 2 && info[1].IsObject()) {
        auto opts = info[1].As<Napi::Object>();
        if (opts.Has("flags") && opts.Get("flags").IsNumber()) {
            flags = opts.Get("flags").As<Napi::Number>().Int32Value();
        }
        if (opts.Has("changeFilter") && opts.Get("changeFilter").IsFunction()) {
            use_v3 = true;
        }
    }

    // Apply runs on the JS thread so conflict/filter callbacks can call JS
    // directly. Crossing threads here races with SQLite's iterator.
    if (dbw->gate().has_pending()) {
        Napi::Error::New(env, "cannot apply changeset while other database "
                              "operations are in flight")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    auto deferred = Napi::Promise::Deferred::New(env);
    Napi::Function conflict_fn;
    Napi::Function table_fn;
    Napi::Function change_fn;
    if (info.Length() >= 2 && info[1].IsObject()) {
        auto opts = info[1].As<Napi::Object>();
        if (opts.Has("onConflict") && opts.Get("onConflict").IsFunction()) {
            conflict_fn = opts.Get("onConflict").As<Napi::Function>();
        }
        if (opts.Has("tableFilter") && opts.Get("tableFilter").IsFunction()) {
            table_fn = opts.Get("tableFilter").As<Napi::Function>();
        }
        if (opts.Has("changeFilter") && opts.Get("changeFilter").IsFunction()) {
            change_fn = opts.Get("changeFilter").As<Napi::Function>();
        }
    }

    try {
        SerialGate::Hold hold(dbw->gate());
        std::string cb_error;
        auto conflict = [&](sqlitex::ConflictType type,
                            const sqlitex::ChangesetIterator& it)
            -> sqlitex::ChangesetAction {
            if (conflict_fn.IsEmpty()) {
                return sqlitex::ChangesetAction::Omit;
            }
            try {
                JsCallbackScope scope;
                auto payload = copied_change_to_js(
                    env, copy_change(it, true, &type));
                auto ret = conflict_fn.Call({payload});
                if (ret.IsPromise()) {
                    throw std::runtime_error(
                        "conflict handler must be synchronous");
                }
                return parse_action(ret);
            } catch (const std::exception& e) {
                cb_error = e.what();
                return sqlitex::ChangesetAction::Abort;
            }
        };
        auto table_filter = [&](std::string_view table) {
            if (table_fn.IsEmpty()) {
                return true;
            }
            try {
                JsCallbackScope scope;
                auto ret = table_fn.Call({Napi::String::New(env, std::string(table))});
                if (ret.IsPromise()) {
                    throw std::runtime_error("table filter must be synchronous");
                }
                return ret.ToBoolean().Value();
            } catch (const std::exception& e) {
                cb_error = e.what();
                return false;
            }
        };
        auto change_filter = [&](const sqlitex::ChangesetIterator& it) {
            if (change_fn.IsEmpty()) {
                return true;
            }
            try {
                JsCallbackScope scope;
                auto ret = change_fn.Call(
                    {copied_change_to_js(env, copy_change(it, false))});
                if (ret.IsPromise()) {
                    throw std::runtime_error("change filter must be synchronous");
                }
                return ret.ToBoolean().Value();
            } catch (const std::exception& e) {
                cb_error = e.what();
                return false;
            }
        };

        try {
            if (use_v3) {
                cs_.apply_v3(dbw->db(), conflict, change_filter, flags);
            } else if (flags != 0) {
                cs_.apply_v2(dbw->db(), conflict, flags);
            } else {
                cs_.apply(dbw->db(), conflict, table_filter);
            }
            if (!cb_error.empty()) {
                throw std::runtime_error(cb_error);
            }
            deferred.Resolve(env.Undefined());
        } catch (const sqlitex::Error& e) {
            if (!cb_error.empty()) {
                deferred.Reject(
                    make_js_error(env, from_exception(std::runtime_error(cb_error)))
                        .Value());
            } else {
                deferred.Reject(make_js_error(env, from_sqlitex(e)).Value());
            }
        } catch (const std::exception& e) {
            deferred.Reject(make_js_error(env, from_exception(e)).Value());
        }
    } catch (const std::exception& e) {
        deferred.Reject(make_js_error(env, from_exception(e)).Value());
    }
    return deferred.Promise();
}

Napi::Object PatchsetWrap::Init(Napi::Env env, Napi::Object exports) {
    auto func = DefineClass(
        env, "NativePatchset",
        {
            InstanceAccessor("size", &PatchsetWrap::Size, nullptr),
            InstanceAccessor("empty", &PatchsetWrap::Empty, nullptr),
            InstanceMethod("toBuffer", &PatchsetWrap::ToBuffer),
            InstanceMethod("apply", &PatchsetWrap::Apply),
        });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("NativePatchset", func);
    return exports;
}

PatchsetWrap::PatchsetWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<PatchsetWrap>(info) {
}

Napi::Object PatchsetWrap::from_native(Napi::Env env, sqlitex::Patchset ps) {
    auto obj = constructor().New({});
    Unwrap(obj)->take(std::move(ps));
    return obj;
}

Napi::Value PatchsetWrap::Size(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), ps_.size());
}

Napi::Value PatchsetWrap::Empty(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), ps_.empty());
}

Napi::Value PatchsetWrap::ToBuffer(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (ps_.empty() || ps_.data() == nullptr) {
        return Napi::Buffer<uint8_t>::Copy(env, nullptr, 0);
    }
    return Napi::Buffer<uint8_t>::Copy(
        env, static_cast<const uint8_t*>(ps_.data()),
        static_cast<size_t>(ps_.size()));
}

Napi::Value PatchsetWrap::Apply(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    DatabaseWrap* dbw = nullptr;
    try {
        dbw = unwrap_checked<DatabaseWrap>(info[0], "Database");
    } catch (const std::exception& e) {
        Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto db_obj = info[0].As<Napi::Object>();

    if (dbw->gate().has_pending()) {
        Napi::Error::New(env, "cannot apply patchset while other database "
                              "operations are in flight")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    auto deferred = Napi::Promise::Deferred::New(env);
    Napi::Function conflict_fn;
    Napi::Function table_fn;
    if (info.Length() >= 2 && info[1].IsObject()) {
        auto opts = info[1].As<Napi::Object>();
        if (opts.Has("onConflict") && opts.Get("onConflict").IsFunction()) {
            conflict_fn = opts.Get("onConflict").As<Napi::Function>();
        }
        if (opts.Has("tableFilter") && opts.Get("tableFilter").IsFunction()) {
            table_fn = opts.Get("tableFilter").As<Napi::Function>();
        }
    }

    try {
        SerialGate::Hold hold(dbw->gate());
        std::string cb_error;
        auto conflict = [&](sqlitex::ConflictType type,
                            const sqlitex::ChangesetIterator& it)
            -> sqlitex::ChangesetAction {
            if (conflict_fn.IsEmpty()) {
                return sqlitex::ChangesetAction::Omit;
            }
            try {
                JsCallbackScope scope;
                auto payload = copied_change_to_js(
                    env, copy_change(it, true, &type));
                auto ret = conflict_fn.Call({payload});
                if (ret.IsPromise()) {
                    throw std::runtime_error(
                        "conflict handler must be synchronous");
                }
                return parse_action(ret);
            } catch (const std::exception& e) {
                cb_error = e.what();
                return sqlitex::ChangesetAction::Abort;
            }
        };
        auto table_filter = [&](std::string_view table) {
            if (table_fn.IsEmpty()) {
                return true;
            }
            try {
                JsCallbackScope scope;
                auto ret = table_fn.Call(
                    {Napi::String::New(env, std::string(table))});
                if (ret.IsPromise()) {
                    throw std::runtime_error("table filter must be synchronous");
                }
                return ret.ToBoolean().Value();
            } catch (const std::exception& e) {
                cb_error = e.what();
                return false;
            }
        };

        try {
            ps_.apply(dbw->db(), conflict, table_filter);
            if (!cb_error.empty()) {
                throw std::runtime_error(cb_error);
            }
            deferred.Resolve(env.Undefined());
        } catch (const sqlitex::Error& e) {
            if (!cb_error.empty()) {
                deferred.Reject(
                    make_js_error(env, from_exception(std::runtime_error(cb_error)))
                        .Value());
            } else {
                deferred.Reject(make_js_error(env, from_sqlitex(e)).Value());
            }
        } catch (const std::exception& e) {
            deferred.Reject(make_js_error(env, from_exception(e)).Value());
        }
    } catch (const std::exception& e) {
        deferred.Reject(make_js_error(env, from_exception(e)).Value());
    }
    return deferred.Promise();
}

} // namespace sqlitex_node

#endif

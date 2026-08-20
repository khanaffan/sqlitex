#include "changegroup_wrap.h"

#ifdef SQLITEX_HAS_SESSION

#include "changeset_wrap.h"

namespace sqlitex_node {

Napi::FunctionReference& ChangegroupWrap::constructor() {
    static thread_local Napi::FunctionReference ref;
    return ref;
}

Napi::Object ChangegroupWrap::Init(Napi::Env env, Napi::Object exports) {
    auto func = DefineClass(env, "NativeChangegroup",
                            {
                                InstanceMethod("add", &ChangegroupWrap::Add),
                                InstanceMethod("output", &ChangegroupWrap::Output),
                            });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("NativeChangegroup", func);
    return exports;
}

ChangegroupWrap::ChangegroupWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ChangegroupWrap>(info) {
}

Napi::Value ChangegroupWrap::Add(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "add() requires a Changeset or Patchset")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto obj = info[0].As<Napi::Object>();
    try {
        if (obj.InstanceOf(ChangesetWrap::constructor().Value())) {
            group_.add(ChangesetWrap::Unwrap(obj)->changeset());
        } else if (obj.InstanceOf(PatchsetWrap::constructor().Value())) {
            group_.add(PatchsetWrap::Unwrap(obj)->patchset());
        } else {
            Napi::TypeError::New(env, "add() requires a Changeset or Patchset")
                .ThrowAsJavaScriptException();
        }
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
    }
    return env.Undefined();
}

Napi::Value ChangegroupWrap::Output(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    try {
        return ChangesetWrap::from_native(env, group_.output());
    } catch (const sqlitex::Error& e) {
        throw_js(env, e);
        return env.Undefined();
    }
}

} // namespace sqlitex_node

#endif

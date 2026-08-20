#pragma once

#include <napi.h>
#include <sqlitex/sqlitex.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace sqlitex_node {

// ============================================================================
// Reentrancy guard — BlockingCall from a JS callback must not re-enter
// native methods that themselves BlockingCall or lock the same SerialGate.
// ============================================================================

inline thread_local int g_js_callback_depth = 0;

struct JsCallbackScope {
    JsCallbackScope() noexcept {
        ++g_js_callback_depth;
    }
    ~JsCallbackScope() {
        --g_js_callback_depth;
    }
};

inline bool in_js_callback() noexcept {
    return g_js_callback_depth > 0;
}

// Captures an exception raised inside a JS callback that is invoked from a C
// callback shim (where the exception cannot propagate). The first error wins;
// it is surfaced on the enclosing operation instead of being swallowed.
class CallbackError {
    public:
    void stash(std::string message) {
        std::lock_guard<std::mutex> lock(mu_);
        if (msg_.empty()) {
            msg_ = std::move(message);
        }
    }

    std::string take() {
        std::lock_guard<std::mutex> lock(mu_);
        std::string out = std::move(msg_);
        msg_.clear();
        return out;
    }

    private:
    std::mutex mu_{};
    std::string msg_{};
};

// Safe downcast of a JS value to a wrapped native instance. napi_unwrap
// succeeds for ANY wrapped object, so the prototype must be checked first or
// an unrelated object gets reinterpreted as the wrong C++ type.
template <typename Wrap>
Wrap* unwrap_checked(Napi::Value value, const char* type_name) {
    if (!value.IsObject() ||
        !value.As<Napi::Object>().InstanceOf(Wrap::constructor().Value())) {
        throw std::invalid_argument(std::string("expected a ") + type_name +
                                    " instance");
    }
    return Wrap::Unwrap(value.As<Napi::Object>());
}

inline void throw_if_reentrant(Napi::Env env, const char* what) {
    if (in_js_callback()) {
        Napi::Error::New(env, std::string("reentrant native call from "
                                          "javascript callback is not allowed: ") +
                                  what)
            .ThrowAsJavaScriptException();
    }
}

// ============================================================================
// Error mapping
// ============================================================================

struct NativeError {
    std::string message;
    int code = -1;
    int extended = 0;
    std::string kind = "error";
    std::string name = "SqlitexError";
};

inline const char* kind_of(const sqlitex::Error& e) {
    if (dynamic_cast<const sqlitex::ConstraintError*>(&e) != nullptr) {
        return "constraint";
    }
    if (dynamic_cast<const sqlitex::BusyError*>(&e) != nullptr) {
        return "busy";
    }
    if (dynamic_cast<const sqlitex::IoError*>(&e) != nullptr) {
        return "io";
    }
    if (dynamic_cast<const sqlitex::CorruptError*>(&e) != nullptr) {
        return "corrupt";
    }
    if (dynamic_cast<const sqlitex::CantOpenError*>(&e) != nullptr) {
        return "cantOpen";
    }
    if (dynamic_cast<const sqlitex::AuthError*>(&e) != nullptr) {
        return "auth";
    }
    if (dynamic_cast<const sqlitex::MisuseError*>(&e) != nullptr) {
        return "misuse";
    }
    if (dynamic_cast<const sqlitex::RangeError*>(&e) != nullptr) {
        return "range";
    }
    if (dynamic_cast<const sqlitex::NotFoundError*>(&e) != nullptr) {
        return "notFound";
    }
    if (dynamic_cast<const sqlitex::NotADbError*>(&e) != nullptr) {
        return "notADb";
    }
    if (dynamic_cast<const sqlitex::SchemaError*>(&e) != nullptr) {
        return "schema";
    }
    if (dynamic_cast<const sqlitex::FullError*>(&e) != nullptr) {
        return "full";
    }
    if (dynamic_cast<const sqlitex::AbortError*>(&e) != nullptr) {
        return "abort";
    }
    if (dynamic_cast<const sqlitex::PermissionDenied*>(&e) != nullptr) {
        return "permission";
    }
    if (dynamic_cast<const sqlitex::ProtocolError*>(&e) != nullptr) {
        return "protocol";
    }
    if (dynamic_cast<const sqlitex::InternalError*>(&e) != nullptr) {
        return "internal";
    }
    if (dynamic_cast<const sqlitex::MismatchError*>(&e) != nullptr) {
        return "mismatch";
    }
    if (dynamic_cast<const sqlitex::ChangesetApplyError*>(&e) != nullptr) {
        return "changesetApply";
    }
    if (dynamic_cast<const sqlitex::ChangesetError*>(&e) != nullptr) {
        return "changeset";
    }
    if (dynamic_cast<const sqlitex::RuntimeError*>(&e) != nullptr) {
        return "runtime";
    }
    return "error";
}

inline const char* name_of(const char* kind) {
    if (std::string(kind) == "constraint") {
        return "ConstraintError";
    }
    if (std::string(kind) == "busy") {
        return "BusyError";
    }
    if (std::string(kind) == "io") {
        return "IoError";
    }
    if (std::string(kind) == "runtime") {
        return "RuntimeError";
    }
    if (std::string(kind) == "changeset" ||
        std::string(kind) == "changesetApply") {
        return "ChangesetError";
    }
    return "SqlitexError";
}

inline NativeError from_sqlitex(const sqlitex::Error& e) {
    NativeError n;
    n.message = e.what();
    n.code = e.code();
    n.extended = e.extended_code();
    n.kind = kind_of(e);
    n.name = name_of(n.kind.c_str());
    return n;
}

inline NativeError from_exception(const std::exception& e) {
    return NativeError{e.what(), -1, 0, "error", "SqlitexError"};
}

inline Napi::Error make_js_error(Napi::Env env, const NativeError& e) {
    auto err = Napi::Error::New(env, e.message);
    err.Set("name", Napi::String::New(env, e.name));
    err.Set("code", Napi::Number::New(env, e.code));
    err.Set("extendedCode", Napi::Number::New(env, e.extended));
    err.Set("kind", Napi::String::New(env, e.kind));
    return err;
}

inline void throw_js(Napi::Env env, const NativeError& e) {
    make_js_error(env, e).ThrowAsJavaScriptException();
}

inline void throw_js(Napi::Env env, const sqlitex::Error& e) {
    throw_js(env, from_sqlitex(e));
}

// ============================================================================
// Per-object serialization
// ============================================================================

class SerialGate {
    public:
    void begin_work() {
        std::lock_guard<std::mutex> lock(mu_);
        if (closing_) {
            throw std::runtime_error("object is closing");
        }
        ++pending_;
    }

    void acquire() {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return !busy_; });
        busy_ = true;
    }

    void release() {
        std::lock_guard<std::mutex> lock(mu_);
        busy_ = false;
        cv_.notify_all();
    }

    void end_work() {
        std::lock_guard<std::mutex> lock(mu_);
        if (pending_ > 0) {
            --pending_;
        }
        cv_.notify_all();
    }

    bool has_pending() const {
        std::lock_guard<std::mutex> lock(mu_);
        return pending_ > 0 || busy_;
    }

    void mark_closing() {
        std::lock_guard<std::mutex> lock(mu_);
        closing_ = true;
    }

    bool is_closing() const {
        std::lock_guard<std::mutex> lock(mu_);
        return closing_;
    }

    // RAII: begin_work + acquire, then release + end_work.
    class Hold {
        public:
        explicit Hold(SerialGate& gate) : gate_(&gate) {
            gate_->begin_work();
            began_ = true;
            gate_->acquire();
            acquired_ = true;
        }
        ~Hold() {
            if (acquired_) {
                gate_->release();
            }
            if (began_) {
                gate_->end_work();
            }
        }
        Hold(const Hold&) = delete;
        Hold& operator=(const Hold&) = delete;

        private:
        SerialGate* gate_;
        bool began_ = false;
        bool acquired_ = false;
    };

    private:
    mutable std::mutex mu_{};
    std::condition_variable cv_{};
    bool busy_ = false;
    int pending_ = 0;
    bool closing_ = false;
};

// ============================================================================
// Promise + AsyncWorker
// ============================================================================

class PromiseWorker : public Napi::AsyncWorker {
    public:
    PromiseWorker(Napi::Env env, Napi::Object owner, SerialGate& gate,
                  std::function<void()> work)
        : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
          owner_(Napi::Persistent(owner)), gate_(&gate), work_(std::move(work)) {
    }

    Napi::Promise Promise() {
        return deferred_.Promise();
    }

    void keep(Napi::Object obj) {
        extras_.emplace_back(Napi::Persistent(obj));
    }

    void set_resolve(std::function<Napi::Value(Napi::Env)> fn) {
        resolve_ = std::move(fn);
    }

    void Execute() override {
        // RAII: any escaping exception type must still release the gate,
        // otherwise every later operation on this handle blocks forever.
        struct Guard {
            SerialGate* g;
            explicit Guard(SerialGate* gate) : g(gate) {
                g->acquire();
            }
            ~Guard() {
                g->release();
            }
        } guard(gate_);
        try {
            work_();
        } catch (const sqlitex::Error& e) {
            err_ = from_sqlitex(e);
        } catch (const std::exception& e) {
            err_ = from_exception(e);
        } catch (...) {
            err_ = NativeError{"unknown native error", SQLITE_ERROR};
        }
    }

    void OnOK() override {
        gate_->end_work();
        if (err_) {
            deferred_.Reject(make_js_error(Env(), *err_).Value());
        } else if (resolve_) {
            try {
                deferred_.Resolve(resolve_(Env()));
            } catch (const Napi::Error& e) {
                deferred_.Reject(e.Value());
            } catch (const std::exception& e) {
                deferred_.Reject(make_js_error(Env(), from_exception(e)).Value());
            }
        } else {
            deferred_.Resolve(Env().Undefined());
        }
        owner_.Reset();
        extras_.clear();
    }

    void OnError(const Napi::Error& e) override {
        gate_->end_work();
        deferred_.Reject(e.Value());
        owner_.Reset();
        extras_.clear();
    }

    private:
    Napi::Promise::Deferred deferred_;
    Napi::ObjectReference owner_;
    std::vector<Napi::ObjectReference> extras_{};
    SerialGate* gate_ = nullptr;
    std::function<void()> work_{};
    std::function<Napi::Value(Napi::Env)> resolve_{};
    std::optional<NativeError> err_{};
};

inline Napi::Value queue_work(Napi::Env env, Napi::Object owner, SerialGate& gate,
                              std::function<void()> work,
                              std::function<Napi::Value(Napi::Env)> resolve = {},
                              std::vector<Napi::Object> extras = {}) {
    throw_if_reentrant(env, "async work");
    try {
        gate.begin_work();
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto* worker = new PromiseWorker(env, owner, gate, std::move(work));
    if (resolve) {
        worker->set_resolve(std::move(resolve));
    }
    for (auto& extra : extras) {
        worker->keep(extra);
    }
    worker->Queue();
    return worker->Promise();
}

// ============================================================================
// Thread-safe JS callbacks
// ============================================================================

class JsCallback {
    public:
    // node-addon-api throws when the underlying release fails (which happens
    // during environment teardown). These run from destructors, so they must
    // never propagate.
    static void safe_abort(Napi::ThreadSafeFunction& fn) noexcept {
        try {
            fn.Abort();
        } catch (...) {
        }
    }

    static void safe_release(Napi::ThreadSafeFunction& fn) noexcept {
        try {
            fn.Release();
        } catch (...) {
        }
    }

    JsCallback() = default;
    ~JsCallback() {
        reset();
    }

    JsCallback(const JsCallback&) = delete;
    JsCallback& operator=(const JsCallback&) = delete;

    void set(Napi::Env env, Napi::Function fn, const char* name,
             size_t max_queue = 0) {
        // Released while the environment is still valid; otherwise the
        // ObjectWrap finalizer runs after napi has already torn the
        // thread-safe function down and aborts inside uv_mutex_lock.
        if (!hook_registered_) {
            napi_add_env_cleanup_hook(env, &JsCallback::env_cleanup, this);
            env_ = env;
            hook_registered_ = true;
        }
        auto fresh = Napi::ThreadSafeFunction::New(env, fn, name, max_queue, 1);
        // A registered callback must not by itself keep the event loop alive;
        // in-flight work is already kept alive by its AsyncWorker.
        fresh.Unref(env);
        Napi::ThreadSafeFunction old;
        bool had_old = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            old = tsfn_;
            had_old = armed_;
            tsfn_ = fresh;
            armed_ = true;
            js_thread_ = std::this_thread::get_id();
        }
        // Aborting outside the lock: any thread that acquired the old handle
        // keeps it alive until it releases.
        if (had_old) {
            safe_abort(old);
        }
    }

    void reset() {
        Napi::ThreadSafeFunction old;
        bool had_old = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            old = tsfn_;
            had_old = armed_;
            tsfn_ = Napi::ThreadSafeFunction();
            armed_ = false;
        }
        if (had_old) {
            safe_abort(old);
        }
        if (hook_registered_) {
            napi_remove_env_cleanup_hook(env_, &JsCallback::env_cleanup, this);
            hook_registered_ = false;
        }
    }

    bool armed() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return armed_;
    }

    // Synchronous round-trip to the JS thread. ThreadSafeFunction::BlockingCall
    // only blocks when the queue is full, so the result must be awaited
    // explicitly.
    template <typename Result>
    Result blocking(std::function<Result(Napi::Env, Napi::Function)> body,
                    Result fallback) {
        auto lease = acquire();
        if (!lease) {
            return fallback;
        }
        if (lease->on_js_thread) {
            throw std::runtime_error(
                "synchronous javascript callback invoked from the javascript "
                "thread would deadlock");
        }

        struct Pack {
            std::function<Result(Napi::Env, Napi::Function)> body;
            Result result;
            std::string error{};
            bool threw = false;
            bool done = false;
            std::mutex mu{};
            std::condition_variable cv{};
        };
        // Heap-allocated and shared with the callback so the callback never
        // touches a destroyed mutex/condvar if the waiter unwinds first.
        auto pack = std::make_shared<Pack>();
        pack->body = std::move(body);
        pack->result = std::move(fallback);

        auto* carrier = new std::shared_ptr<Pack>(pack);
        auto status = lease->tsfn.BlockingCall(
            carrier, [](Napi::Env env, Napi::Function js,
                        std::shared_ptr<Pack>* owned_carrier) {
                std::unique_ptr<std::shared_ptr<Pack>> owned(owned_carrier);
                auto p = *owned;
                std::lock_guard<std::mutex> lock(p->mu);
                if (env == nullptr || js == nullptr) {
                    p->threw = true;
                    p->error = "javascript callback unavailable";
                } else {
                    JsCallbackScope scope;
                    try {
                        p->result = p->body(env, js);
                    } catch (const Napi::Error& e) {
                        p->threw = true;
                        p->error = e.Message();
                    } catch (const std::exception& e) {
                        p->threw = true;
                        p->error = e.what();
                    } catch (...) {
                        p->threw = true;
                        p->error = "javascript callback failed";
                    }
                }
                p->done = true;
                p->cv.notify_all();
            });
        if (status != napi_ok) {
            delete carrier;
            throw std::runtime_error("failed to invoke javascript callback");
        }
        {
            std::unique_lock<std::mutex> lock(pack->mu);
            pack->cv.wait(lock, [&] { return pack->done; });
        }
        if (pack->threw) {
            throw std::runtime_error(pack->error);
        }
        return pack->result;
    }

    // Fire-and-forget. Overflow policy: drop.
    void nonblocking(std::string message) {
        auto lease = acquire();
        if (!lease) {
            return;
        }
        auto* heap = new std::string(std::move(message));
        auto status = lease->tsfn.NonBlockingCall(
            heap, [](Napi::Env env, Napi::Function js, std::string* msg) {
                std::unique_ptr<std::string> owned(msg);
                if (env == nullptr || js == nullptr) {
                    return;
                }
                JsCallbackScope scope;
                try {
                    js.Call({Napi::String::New(env, *owned)});
                } catch (...) {
                    // Log callbacks must never throw across the C boundary.
                }
            });
        if (status != napi_ok) {
            delete heap;
        }
    }

    private:
    // Keeps the thread-safe function alive for the duration of a call even if
    // another thread swaps or clears the callback concurrently.
    struct Lease {
        Napi::ThreadSafeFunction tsfn;
        bool on_js_thread = false;

        explicit Lease(Napi::ThreadSafeFunction fn, bool js)
            : tsfn(std::move(fn)), on_js_thread(js) {
        }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() {
            safe_release(tsfn);
        }
    };

    std::unique_ptr<Lease> acquire() {
        std::lock_guard<std::mutex> lock(mu_);
        if (!armed_) {
            return nullptr;
        }
        if (tsfn_.Acquire() != napi_ok) {
            return nullptr;
        }
        return std::make_unique<Lease>(tsfn_,
                                       std::this_thread::get_id() == js_thread_);
    }

    static void env_cleanup(void* arg) {
        auto* self = static_cast<JsCallback*>(arg);
        // The hook is removed by napi as it fires.
        self->hook_registered_ = false;
        self->reset();
    }

    mutable std::mutex mu_{};
    Napi::ThreadSafeFunction tsfn_{};
    bool armed_ = false;
    std::thread::id js_thread_{};
    napi_env env_ = nullptr;
    bool hook_registered_ = false;
};

// ============================================================================
// Value conversion
// ============================================================================

inline Napi::Value sqlite_value_to_js(Napi::Env env, sqlite3_value* v) {
    if (v == nullptr) {
        return env.Null();
    }
    switch (sqlite3_value_type(v)) {
    case SQLITE_INTEGER: {
        const int64_t i = sqlite3_value_int64(v);
        constexpr int64_t kMax = 9007199254740991LL;
        if (i > kMax || i < -kMax) {
            return Napi::BigInt::New(env, i);
        }
        return Napi::Number::New(env, static_cast<double>(i));
    }
    case SQLITE_FLOAT:
        return Napi::Number::New(env, sqlite3_value_double(v));
    case SQLITE_TEXT: {
        const auto* text =
            reinterpret_cast<const char*>(sqlite3_value_text(v));
        const int n = sqlite3_value_bytes(v);
        if (text == nullptr) {
            return env.Null();
        }
        return Napi::String::New(env, text, static_cast<size_t>(n));
    }
    case SQLITE_BLOB: {
        const auto* blob =
            static_cast<const uint8_t*>(sqlite3_value_blob(v));
        const int n = sqlite3_value_bytes(v);
        if (blob == nullptr || n <= 0) {
            return Napi::Buffer<uint8_t>::Copy(env, nullptr, 0);
        }
        return Napi::Buffer<uint8_t>::Copy(env, blob, static_cast<size_t>(n));
    }
    default:
        return env.Null();
    }
}

inline Napi::Value value_to_js(Napi::Env env, const sqlitex::Value& v) {
    switch (v.type()) {
    case sqlitex::Value::Type::Null:
        return env.Null();
    case sqlitex::Value::Type::Integer: {
        const int64_t i = v.get_int();
        constexpr int64_t kMax = 9007199254740991LL;
        if (i > kMax || i < -kMax) {
            return Napi::BigInt::New(env, i);
        }
        return Napi::Number::New(env, static_cast<double>(i));
    }
    case sqlitex::Value::Type::Float:
        return Napi::Number::New(env, v.get_double());
    case sqlitex::Value::Type::Text:
        return Napi::String::New(env, std::string(v.get_text()));
    case sqlitex::Value::Type::Blob: {
        const auto& blob = v.get_blob();
        if (blob.empty()) {
            return Napi::Buffer<uint8_t>::Copy(env, nullptr, 0);
        }
        return Napi::Buffer<uint8_t>::Copy(env, blob.data(), blob.size());
    }
    }
    return env.Undefined();
}

inline std::string require_string(const Napi::Value& v, const char* name) {
    if (!v.IsString()) {
        throw std::invalid_argument(std::string(name) + " must be a string");
    }
    return v.As<Napi::String>().Utf8Value();
}

#ifdef SQLITEX_HAS_SESSION
inline const char* op_name(sqlitex::ChangeOperation op) {
    switch (op) {
    case sqlitex::ChangeOperation::Insert:
        return "insert";
    case sqlitex::ChangeOperation::Delete:
        return "delete";
    case sqlitex::ChangeOperation::Update:
        return "update";
    }
    return "unknown";
}

inline const char* conflict_name(sqlitex::ConflictType t) {
    switch (t) {
    case sqlitex::ConflictType::Data:
        return "data";
    case sqlitex::ConflictType::NotFound:
        return "notFound";
    case sqlitex::ConflictType::Conflict:
        return "conflict";
    case sqlitex::ConflictType::Constraint:
        return "constraint";
    case sqlitex::ConflictType::ForeignKey:
        return "foreignKey";
    }
    return "unknown";
}

struct CopiedValue {
    enum class Kind { Null, Integer, Float, Text, Blob };
    Kind kind = Kind::Null;
    int64_t i = 0;
    double d = 0;
    std::string text;
    std::vector<uint8_t> blob;
};

struct CopiedChange {
    std::string op;
    std::string table;
    int column_count = 0;
    bool indirect = false;
    std::vector<char> primary_key;
    std::vector<CopiedValue> old_values;
    std::vector<CopiedValue> new_values;
    std::vector<CopiedValue> conflict_values;
    int fk_conflicts = 0;
    std::string conflict_type;
};

inline CopiedValue copy_sqlite_value(sqlite3_value* v) {
    CopiedValue out;
    if (v == nullptr) {
        return out;
    }
    switch (sqlite3_value_type(v)) {
    case SQLITE_INTEGER:
        out.kind = CopiedValue::Kind::Integer;
        out.i = sqlite3_value_int64(v);
        break;
    case SQLITE_FLOAT:
        out.kind = CopiedValue::Kind::Float;
        out.d = sqlite3_value_double(v);
        break;
    case SQLITE_TEXT: {
        out.kind = CopiedValue::Kind::Text;
        const auto* text =
            reinterpret_cast<const char*>(sqlite3_value_text(v));
        const int n = sqlite3_value_bytes(v);
        if (text != nullptr) {
            out.text.assign(text, static_cast<size_t>(n));
        }
        break;
    }
    case SQLITE_BLOB: {
        out.kind = CopiedValue::Kind::Blob;
        const auto* blob = static_cast<const uint8_t*>(sqlite3_value_blob(v));
        const int n = sqlite3_value_bytes(v);
        if (blob != nullptr && n > 0) {
            out.blob.assign(blob, blob + n);
        }
        break;
    }
    default:
        break;
    }
    return out;
}

inline std::vector<CopiedValue>
copy_columns(sqlite3_changeset_iter* iter,
             int (*getter)(sqlite3_changeset_iter*, int, sqlite3_value**),
             int ncols) {
    std::vector<CopiedValue> out(static_cast<size_t>(ncols));
    for (int i = 0; i < ncols; ++i) {
        sqlite3_value* val = nullptr;
        if (getter(iter, i, &val) == SQLITE_OK) {
            out[static_cast<size_t>(i)] = copy_sqlite_value(val);
        }
    }
    return out;
}

inline CopiedChange copy_change(const sqlitex::ChangesetIterator& it,
                                bool include_conflict,
                                const sqlitex::ConflictType* ctype = nullptr) {
    CopiedChange out;
    const bool fk =
        include_conflict && ctype != nullptr &&
        *ctype == sqlitex::ConflictType::ForeignKey;
    auto* handle = const_cast<sqlitex::ChangesetIterator&>(it).handle();
    if (fk) {
        // For FOREIGN_KEY conflicts the iterator carries no op, table, or
        // column data — sqlite3changeset_op() returns SQLITE_MISUSE. Only the
        // conflict count is available.
        out.op = "";
        out.conflict_type = conflict_name(*ctype);
        int n = 0;
        if (sqlite3changeset_fk_conflicts(handle, &n) == SQLITE_OK) {
            out.fk_conflicts = n;
        }
        return out;
    }
    out.op = op_name(it.op());
    out.table = it.table_name();
    out.column_count = it.column_count();
    out.indirect = it.is_indirect();
    auto pk = it.primary_key();
    out.primary_key.resize(pk.size());
    for (size_t i = 0; i < pk.size(); ++i) {
        out.primary_key[i] = pk[i] ? 1 : 0;
    }
    out.old_values = copy_columns(handle, sqlite3changeset_old, out.column_count);
    out.new_values = copy_columns(handle, sqlite3changeset_new, out.column_count);
    if (include_conflict) {
        if (ctype != nullptr) {
            out.conflict_type = conflict_name(*ctype);
        }
        out.conflict_values =
            copy_columns(handle, sqlite3changeset_conflict, out.column_count);
    }
    return out;
}

inline Napi::Value copied_value_to_js(Napi::Env env, const CopiedValue& v) {
    switch (v.kind) {
    case CopiedValue::Kind::Integer: {
        constexpr int64_t kMax = 9007199254740991LL;
        if (v.i > kMax || v.i < -kMax) {
            return Napi::BigInt::New(env, v.i);
        }
        return Napi::Number::New(env, static_cast<double>(v.i));
    }
    case CopiedValue::Kind::Float:
        return Napi::Number::New(env, v.d);
    case CopiedValue::Kind::Text:
        return Napi::String::New(env, v.text);
    case CopiedValue::Kind::Blob:
        if (v.blob.empty()) {
            return Napi::Buffer<uint8_t>::Copy(env, nullptr, 0);
        }
        return Napi::Buffer<uint8_t>::Copy(env, v.blob.data(), v.blob.size());
    case CopiedValue::Kind::Null:
    default:
        return env.Null();
    }
}

inline Napi::Array copied_values_to_js(Napi::Env env,
                                       const std::vector<CopiedValue>& vals) {
    auto arr = Napi::Array::New(env, vals.size());
    for (size_t i = 0; i < vals.size(); ++i) {
        arr.Set(static_cast<uint32_t>(i), copied_value_to_js(env, vals[i]));
    }
    return arr;
}

inline Napi::Object copied_change_to_js(Napi::Env env, const CopiedChange& ch) {
    auto obj = Napi::Object::New(env);
    obj.Set("op", Napi::String::New(env, ch.op));
    obj.Set("table", Napi::String::New(env, ch.table));
    obj.Set("columnCount", Napi::Number::New(env, ch.column_count));
    obj.Set("indirect", Napi::Boolean::New(env, ch.indirect));
    auto pk = Napi::Array::New(env, ch.primary_key.size());
    for (size_t i = 0; i < ch.primary_key.size(); ++i) {
        pk.Set(static_cast<uint32_t>(i),
               Napi::Boolean::New(env, ch.primary_key[i] != 0));
    }
    obj.Set("primaryKey", pk);
    obj.Set("oldValues", copied_values_to_js(env, ch.old_values));
    obj.Set("newValues", copied_values_to_js(env, ch.new_values));
    if (!ch.conflict_type.empty()) {
        obj.Set("type", Napi::String::New(env, ch.conflict_type));
        obj.Set("conflictValues", copied_values_to_js(env, ch.conflict_values));
        obj.Set("fkConflicts", Napi::Number::New(env, ch.fk_conflicts));
    }
    return obj;
}
#endif

} // namespace sqlitex_node

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace sqlitex {
namespace detail {

/// Helper to store a std::function on the heap and expose a void* context
/// pointer for passing to C callback shims.  The C shim casts ctx back to
/// the concrete std::function type and invokes it.
///
/// Usage:
///
///   using Fn = std::function<int(const char*)>;
///   auto ctx = CallbackContext::create<Fn>([](const char* s) { ... });
///   c_api_register(ctx.c_ptr(), my_c_shim);
///
/// The std::function is kept alive as long as the CallbackContext exists.
template <typename Func>
class CallbackContext {
    public:
    /// Create a new context that owns the given callable.
    static CallbackContext create(Func fn) {
        auto* ptr = new Func(std::move(fn));
        return CallbackContext(ptr);
    }

    CallbackContext() = default;

    CallbackContext(CallbackContext&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)) {
    }

    CallbackContext& operator=(CallbackContext&& other) noexcept {
        if (this != &other) {
            ptr_.reset(std::exchange(other.ptr_, nullptr));
        }
        return *this;
    }

    CallbackContext(const CallbackContext&) = delete;
    CallbackContext& operator=(const CallbackContext&) = delete;

    ~CallbackContext() = default;

    /// Returns true if a function is stored.
    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    /// Returns the raw void* for passing as C callback context.
    void* c_ptr() const noexcept {
        return static_cast<void*>(ptr_.get());
    }

    /// Returns a reference to the stored function.
    Func& func() {
        return *ptr_;
    }
    const Func& func() const {
        return *ptr_;
    }

    private:
    explicit CallbackContext(Func* ptr) : ptr_(ptr) {
    }

    struct Deleter {
        void operator()(Func* p) const noexcept {
            delete p;
        }
    };
    std::unique_ptr<Func, Deleter> ptr_;
};

/// Convenience: make a C callback context + extract raw pointer in one step.
template <typename Func>
std::pair<CallbackContext<Func>, void*> make_context(Func fn) {
    auto ctx = CallbackContext<Func>::create(std::move(fn));
    void* raw = ctx.c_ptr();
    return {std::move(ctx), raw};
}

} // namespace detail
} // namespace sqlitex

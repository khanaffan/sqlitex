#include "sqlitex/fts5.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sqlitex {

namespace {

int emit_to_c_callback(void* pCtx, int tflags, const char* pToken, int nToken,
                       int iStart, int iEnd) noexcept {
    auto* emit = static_cast<const Fts5TokenEmit*>(pCtx);
    try {
        std::string_view tok(pToken ? pToken : "",
                             nToken > 0 ? static_cast<std::size_t>(nToken) : 0);
        return (*emit)(tflags, tok, iStart, iEnd);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

struct CustomTokenizerHolder {
    std::unique_ptr<Fts5Tokenizer> impl;
};

void destroy_tokenizer_factory(void* ptr) noexcept {
    delete static_cast<Fts5TokenizerFactory*>(ptr);
}

int tokenizer_create_shim(void* pCtx, const char** azArg, int nArg,
                          ::Fts5Tokenizer** ppOut) noexcept {
    auto* factory = static_cast<Fts5TokenizerFactory*>(pCtx);
    try {
        std::vector<std::string_view> args;
        args.reserve(nArg > 0 ? static_cast<std::size_t>(nArg) : 0);
        for (int i = 0; i < nArg; ++i) {
            args.emplace_back(azArg[i] ? azArg[i] : "");
        }
        auto impl = (*factory)(args);
        if (!impl) {
            return SQLITE_ERROR;
        }
        auto* holder = new CustomTokenizerHolder{std::move(impl)};
        *ppOut = reinterpret_cast<::Fts5Tokenizer*>(holder);
        return SQLITE_OK;
    } catch (const Error& e) {
        return static_cast<int>(e.code());
    } catch (...) {
        return SQLITE_ERROR;
    }
}

void tokenizer_delete_shim(::Fts5Tokenizer* pTok) noexcept {
    delete reinterpret_cast<CustomTokenizerHolder*>(pTok);
}

int tokenizer_v2_tokenize_shim(::Fts5Tokenizer* pTok, void* pCtx, int flags,
                               const char* pText, int nText,
                               const char* pLocale, int nLocale,
                               int (*xToken)(void*, int, const char*, int, int,
                                             int)) noexcept {
    auto* holder = reinterpret_cast<CustomTokenizerHolder*>(pTok);
    try {
        std::string_view text(pText ? pText : "",
                              nText > 0 ? static_cast<std::size_t>(nText) : 0);
        std::string_view locale(
            pLocale ? pLocale : "",
            nLocale > 0 ? static_cast<std::size_t>(nLocale) : 0);
        Fts5TokenEmit emit = [pCtx, xToken](int tflags, std::string_view tok,
                                            int start, int end) -> int {
            return xToken(pCtx, tflags, tok.empty() ? "" : tok.data(),
                          static_cast<int>(tok.size()), start, end);
        };
        return holder->impl->tokenize(flags, text, locale, emit);
    } catch (const Error& e) {
        return static_cast<int>(e.code());
    } catch (...) {
        return SQLITE_ERROR;
    }
}

int tokenizer_v1_tokenize_shim(::Fts5Tokenizer* pTok, void* pCtx, int flags,
                               const char* pText, int nText,
                               int (*xToken)(void*, int, const char*, int, int,
                                             int)) noexcept {
    return tokenizer_v2_tokenize_shim(pTok, pCtx, flags, pText, nText, nullptr,
                                      0, xToken);
}

void destroy_aux_function(void* ptr) noexcept {
    delete static_cast<Fts5AuxFunction*>(ptr);
}

void aux_function_shim(const Fts5ExtensionApi* pApi, Fts5Context* pFts,
                       sqlite3_context* pCtx, int nVal,
                       sqlite3_value** apVal) noexcept {
    auto* fn = static_cast<Fts5AuxFunction*>(pApi->xUserData(pFts));
    try {
        Fts5ExtensionContext fts5_ctx(pApi, pFts);
        FunctionContext sql_ctx(pCtx);
        std::vector<ValueRef> args;
        args.reserve(nVal > 0 ? static_cast<std::size_t>(nVal) : 0);
        for (int i = 0; i < nVal; ++i) {
            args.emplace_back(apVal[i]);
        }
        (*fn)(fts5_ctx, sql_ctx, args);
    } catch (const Error& e) {
        sqlite3_result_error(pCtx, e.what(), -1);
    } catch (const std::exception& e) {
        sqlite3_result_error(pCtx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(pCtx, "unknown error in FTS5 auxiliary function",
                             -1);
    }
}

int query_phrase_shim(const Fts5ExtensionApi* pApi, Fts5Context* pFts,
                      void* pUserData) noexcept {
    auto* fn =
        static_cast<const std::function<void(const Fts5ExtensionContext&)>*>(
            pUserData);
    try {
        Fts5ExtensionContext ctx(pApi, pFts);
        (*fn)(ctx);
        return SQLITE_OK;
    } catch (const Error& e) {
        return static_cast<int>(e.code());
    } catch (...) {
        return SQLITE_ERROR;
    }
}

} // anonymous namespace

// ============================================================================
// Fts5ExtensionContext
// ============================================================================

int Fts5ExtensionContext::api_version() const noexcept {
    return api_->iVersion;
}

int Fts5ExtensionContext::column_count() const noexcept {
    return api_->xColumnCount(ctx_);
}

int64_t Fts5ExtensionContext::row_count() const {
    sqlite3_int64 n = 0;
    int const rc = api_->xRowCount(ctx_, &n);
    throw_on_error(rc, "FTS5 xRowCount failed");
    return n;
}

int64_t Fts5ExtensionContext::column_total_size(int col) const {
    sqlite3_int64 n = 0;
    int const rc = api_->xColumnTotalSize(ctx_, col, &n);
    throw_on_error(rc, "FTS5 xColumnTotalSize failed");
    return n;
}

int Fts5ExtensionContext::phrase_count() const noexcept {
    return api_->xPhraseCount(ctx_);
}

int Fts5ExtensionContext::phrase_size(int phrase) const noexcept {
    return api_->xPhraseSize(ctx_, phrase);
}

int Fts5ExtensionContext::inst_count() const {
    int n = 0;
    int const rc = api_->xInstCount(ctx_, &n);
    throw_on_error(rc, "FTS5 xInstCount failed");
    return n;
}

Fts5Instance Fts5ExtensionContext::inst(int idx) const {
    Fts5Instance out{};
    int const rc =
        api_->xInst(ctx_, idx, &out.phrase, &out.column, &out.offset);
    throw_on_error(rc, "FTS5 xInst failed");
    return out;
}

int64_t Fts5ExtensionContext::rowid() const noexcept {
    return api_->xRowid(ctx_);
}

std::string_view Fts5ExtensionContext::column_text(int col) const {
    const char* z = nullptr;
    int n = 0;
    int const rc = api_->xColumnText(ctx_, col, &z, &n);
    throw_on_error(rc, "FTS5 xColumnText failed");
    if (z == nullptr || n <= 0) {
        return {};
    }
    return {z, static_cast<std::size_t>(n)};
}

int Fts5ExtensionContext::column_size(int col) const {
    int n = 0;
    int const rc = api_->xColumnSize(ctx_, col, &n);
    throw_on_error(rc, "FTS5 xColumnSize failed");
    return n;
}

std::string_view Fts5ExtensionContext::column_locale(int col) const {
    if (api_->iVersion < 4 || api_->xColumnLocale == nullptr) {
        return {};
    }
    const char* z = nullptr;
    int n = 0;
    int const rc = api_->xColumnLocale(ctx_, col, &z, &n);
    throw_on_error(rc, "FTS5 xColumnLocale failed");
    if (z == nullptr || n <= 0) {
        return {};
    }
    return {z, static_cast<std::size_t>(n)};
}

std::string_view Fts5ExtensionContext::query_token(int phrase,
                                                   int token) const {
    if (api_->iVersion < 3 || api_->xQueryToken == nullptr) {
        throw Error(ErrorCode::Misuse, "FTS5 xQueryToken requires API v3+");
    }
    const char* z = nullptr;
    int n = 0;
    int const rc = api_->xQueryToken(ctx_, phrase, token, &z, &n);
    throw_on_error(rc, "FTS5 xQueryToken failed");
    if (z == nullptr || n <= 0) {
        return {};
    }
    return {z, static_cast<std::size_t>(n)};
}

std::string_view Fts5ExtensionContext::inst_token(int idx, int token) const {
    if (api_->iVersion < 3 || api_->xInstToken == nullptr) {
        throw Error(ErrorCode::Misuse, "FTS5 xInstToken requires API v3+");
    }
    const char* z = nullptr;
    int n = 0;
    int const rc = api_->xInstToken(ctx_, idx, token, &z, &n);
    throw_on_error(rc, "FTS5 xInstToken failed");
    if (z == nullptr || n <= 0) {
        return {};
    }
    return {z, static_cast<std::size_t>(n)};
}

void Fts5ExtensionContext::tokenize(std::string_view text,
                                    const Fts5TokenEmit& on_token) const {
    int const rc = api_->xTokenize(
        ctx_, text.empty() ? "" : text.data(), static_cast<int>(text.size()),
        const_cast<Fts5TokenEmit*>(&on_token), emit_to_c_callback);
    if (rc != SQLITE_OK && rc != SQLITE_DONE) {
        throw_on_error(rc, "FTS5 xTokenize failed");
    }
}

void Fts5ExtensionContext::tokenize(std::string_view text,
                                    std::string_view locale,
                                    const Fts5TokenEmit& on_token) const {
    if (api_->iVersion >= 4 && api_->xTokenize_v2 != nullptr) {
        int const rc = api_->xTokenize_v2(
            ctx_, text.empty() ? "" : text.data(),
            static_cast<int>(text.size()),
            locale.empty() ? nullptr : locale.data(),
            static_cast<int>(locale.size()),
            const_cast<Fts5TokenEmit*>(&on_token), emit_to_c_callback);
        if (rc != SQLITE_OK && rc != SQLITE_DONE) {
            throw_on_error(rc, "FTS5 xTokenize_v2 failed");
        }
        return;
    }
    tokenize(text, on_token);
}

void Fts5ExtensionContext::for_each_phrase_occurrence(
    int phrase, const std::function<void(int col, int offset)>& fn) const {
    Fts5PhraseIter iter{};
    int col = 0;
    int off = 0;
    int const rc = api_->xPhraseFirst(ctx_, phrase, &iter, &col, &off);
    throw_on_error(rc, "FTS5 xPhraseFirst failed");
    while (col >= 0) {
        fn(col, off);
        api_->xPhraseNext(ctx_, &iter, &col, &off);
    }
}

void Fts5ExtensionContext::for_each_phrase_column(
    int phrase, const std::function<void(int col)>& fn) const {
    Fts5PhraseIter iter{};
    int col = 0;
    int const rc = api_->xPhraseFirstColumn(ctx_, phrase, &iter, &col);
    throw_on_error(rc, "FTS5 xPhraseFirstColumn failed");
    while (col >= 0) {
        fn(col);
        api_->xPhraseNextColumn(ctx_, &iter, &col);
    }
}

void Fts5ExtensionContext::query_phrase(
    int phrase,
    const std::function<void(const Fts5ExtensionContext&)>& fn) const {
    int const rc = api_->xQueryPhrase(
        ctx_, phrase,
        const_cast<std::function<void(const Fts5ExtensionContext&)>*>(&fn),
        query_phrase_shim);
    throw_on_error(rc, "FTS5 xQueryPhrase failed");
}

void Fts5ExtensionContext::set_auxdata(void* data,
                                       void (*destroy)(void*)) const {
    int const rc = api_->xSetAuxdata(ctx_, data, destroy);
    throw_on_error(rc, "FTS5 xSetAuxdata failed");
}

void* Fts5ExtensionContext::get_auxdata(bool clear) const noexcept {
    return api_->xGetAuxdata(ctx_, clear ? 1 : 0);
}

// ============================================================================
// Fts5Api
// ============================================================================

Fts5Api Fts5Api::from_database(Database& db) {
    sqlite3_stmt* stmt = nullptr;
    int rc =
        sqlite3_prepare_v2(db.handle(), "SELECT fts5(?1)", -1, &stmt, nullptr);
    throw_on_error(rc, db.handle());

    fts5_api* p_api = nullptr;
    rc = sqlite3_bind_pointer(stmt, 1, static_cast<void*>(&p_api),
                              "fts5_api_ptr", nullptr);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW || rc == SQLITE_DONE) {
            rc = SQLITE_OK;
        }
    }
    sqlite3_finalize(stmt);
    throw_on_error(rc, db.handle());

    if (p_api == nullptr) {
        throw Error(ErrorCode::Error, "Failed to retrieve FTS5 API pointer");
    }
    return Fts5Api(p_api);
}

int Fts5Api::version() const noexcept {
    return api_ ? api_->iVersion : 0;
}

void Fts5Api::create_tokenizer(std::string_view name,
                               Fts5TokenizerFactory factory) {
    if (!factory) {
        throw Error(ErrorCode::Misuse, "FTS5 tokenizer factory is empty");
    }
    std::string name_str(name);
    auto owned = std::make_unique<Fts5TokenizerFactory>(std::move(factory));

    int rc = SQLITE_ERROR;
    if (api_->iVersion >= 3 && api_->xCreateTokenizer_v2 != nullptr) {
        fts5_tokenizer_v2 tok{};
        tok.iVersion = 2;
        tok.xCreate = tokenizer_create_shim;
        tok.xDelete = tokenizer_delete_shim;
        tok.xTokenize = tokenizer_v2_tokenize_shim;
        rc = api_->xCreateTokenizer_v2(api_, name_str.c_str(), owned.get(),
                                       &tok, destroy_tokenizer_factory);
    } else {
        fts5_tokenizer tok{};
        tok.xCreate = tokenizer_create_shim;
        tok.xDelete = tokenizer_delete_shim;
        tok.xTokenize = tokenizer_v1_tokenize_shim;
        rc = api_->xCreateTokenizer(api_, name_str.c_str(), owned.get(), &tok,
                                    destroy_tokenizer_factory);
    }
    throw_on_error(rc, "FTS5 create_tokenizer failed");
    owned.release();
}

bool Fts5Api::has_tokenizer(std::string_view name) const {
    std::string name_str(name);
    void* user_data = nullptr;
    if (api_->iVersion >= 3 && api_->xFindTokenizer_v2 != nullptr) {
        fts5_tokenizer_v2* tok = nullptr;
        return api_->xFindTokenizer_v2(api_, name_str.c_str(), &user_data,
                                       &tok) == SQLITE_OK &&
               tok != nullptr;
    }
    fts5_tokenizer tok{};
    return api_->xFindTokenizer(api_, name_str.c_str(), &user_data, &tok) ==
           SQLITE_OK;
}

void Fts5Api::tokenize_with(std::string_view tokenizer_name,
                            const std::vector<std::string>& args, int flags,
                            std::string_view text,
                            const Fts5TokenEmit& on_token,
                            std::string_view locale) const {
    std::string name_str(tokenizer_name);
    std::vector<const char*> c_args;
    c_args.reserve(args.size());
    for (const auto& a : args) {
        c_args.push_back(a.c_str());
    }

    void* user_data = nullptr;
    if (api_->iVersion >= 3 && api_->xFindTokenizer_v2 != nullptr) {
        fts5_tokenizer_v2* tok = nullptr;
        int rc =
            api_->xFindTokenizer_v2(api_, name_str.c_str(), &user_data, &tok);
        throw_on_error(rc, "FTS5 xFindTokenizer_v2 failed");

        ::Fts5Tokenizer* inst = nullptr;
        rc = tok->xCreate(user_data, c_args.empty() ? nullptr : c_args.data(),
                          static_cast<int>(c_args.size()), &inst);
        throw_on_error(rc, "FTS5 tokenizer xCreate failed");

        struct TokGuard {
            fts5_tokenizer_v2* t;
            ::Fts5Tokenizer* i;
            ~TokGuard() {
                if (i != nullptr) {
                    t->xDelete(i);
                }
            }
        } guard{tok, inst};

        rc = tok->xTokenize(
            inst, const_cast<Fts5TokenEmit*>(&on_token), flags,
            text.empty() ? "" : text.data(), static_cast<int>(text.size()),
            locale.empty() ? nullptr : locale.data(),
            static_cast<int>(locale.size()), emit_to_c_callback);
        if (rc != SQLITE_OK && rc != SQLITE_DONE) {
            throw_on_error(rc, "FTS5 tokenizer xTokenize failed");
        }
        return;
    }

    fts5_tokenizer tok{};
    int rc = api_->xFindTokenizer(api_, name_str.c_str(), &user_data, &tok);
    throw_on_error(rc, "FTS5 xFindTokenizer failed");

    ::Fts5Tokenizer* inst = nullptr;
    rc = tok.xCreate(user_data, c_args.empty() ? nullptr : c_args.data(),
                     static_cast<int>(c_args.size()), &inst);
    throw_on_error(rc, "FTS5 tokenizer xCreate failed");

    struct TokGuardV1 {
        fts5_tokenizer* t;
        ::Fts5Tokenizer* i;
        ~TokGuardV1() {
            if (i != nullptr) {
                t->xDelete(i);
            }
        }
    } guard{&tok, inst};

    rc = tok.xTokenize(inst, const_cast<Fts5TokenEmit*>(&on_token), flags,
                       text.empty() ? "" : text.data(),
                       static_cast<int>(text.size()), emit_to_c_callback);
    if (rc != SQLITE_OK && rc != SQLITE_DONE) {
        throw_on_error(rc, "FTS5 tokenizer xTokenize failed");
    }
}

void Fts5Api::create_function(std::string_view name, Fts5AuxFunction fn) {
    if (!fn) {
        throw Error(ErrorCode::Misuse, "FTS5 auxiliary function is empty");
    }
    std::string name_str(name);
    auto owned = std::make_unique<Fts5AuxFunction>(std::move(fn));
    int const rc =
        api_->xCreateFunction(api_, name_str.c_str(), owned.get(),
                              aux_function_shim, destroy_aux_function);
    throw_on_error(rc, "FTS5 xCreateFunction failed");
    owned.release();
}

} // namespace sqlitex

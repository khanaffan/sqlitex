#include "database_wrap.h"

#ifdef SQLITEX_HAS_SESSION
#include "session_wrap.h"
#include "changeset_wrap.h"
#include "changegroup_wrap.h"
#endif

#ifdef SQLITEX_HAS_BCVFS
#include "bcvfs_wrap.h"
#include "container_wrap.h"
#endif

namespace sqlitex_node {

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    auto caps = Napi::Object::New(env);
#ifdef SQLITEX_HAS_BCVFS
    caps.Set("hasBcvfs", Napi::Boolean::New(env, true));
#else
    caps.Set("hasBcvfs", Napi::Boolean::New(env, false));
#endif
#ifdef SQLITEX_HAS_SESSION
    caps.Set("hasSession", Napi::Boolean::New(env, true));
#else
    caps.Set("hasSession", Napi::Boolean::New(env, false));
#endif
    exports.Set("capabilities", caps);

    DatabaseWrap::Init(env, exports);

#ifdef SQLITEX_HAS_SESSION
    SessionWrap::Init(env, exports);
    ChangesetWrap::Init(env, exports);
    PatchsetWrap::Init(env, exports);
    ChangegroupWrap::Init(env, exports);
#endif

#ifdef SQLITEX_HAS_BCVFS
    BcvfsWrap::Init(env, exports);
    BcvContainerWrap::Init(env, exports);
#endif

    return exports;
}

} // namespace sqlitex_node

Napi::Object InitAddon(Napi::Env env, Napi::Object exports) {
    return sqlitex_node::Init(env, exports);
}

NODE_API_MODULE(sqlitex_node, InitAddon)

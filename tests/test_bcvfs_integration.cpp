// ============================================================================
// bcVFS Integration Tests — Exercises the bcVFS lifecycle against Azurite
//
// These tests require a running Azurite instance at 127.0.0.1:10000.
// If Azurite is not running, all tests are skipped via GTEST_SKIP().
//
// Start Azurite:  azurite --silent --location /tmp/azurite
// ============================================================================

#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace sqlitex {
namespace {

// ============================================================================
// Constants
// ============================================================================

constexpr const char* kAzuriteHost = "127.0.0.1";
constexpr int kAzuritePort = 10000;
constexpr const char* kAzuriteAccount = "devstoreaccount1";
constexpr const char* kAzuriteKey =
    "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/"
    "K1SZFPTOtr/KBHBeksoGMGw==";

const std::string kStorage = "azure?emulator=127.0.0.1:10000";
constexpr const char* kVfsName = "bcvfs-test";
constexpr const char* kContainerPrefix = "bcvfsint";

// ============================================================================
// bcVFS C API helpers (extern "C" — symbols resolved from libsqlitex.a)
// ============================================================================

extern "C" {
struct sqlite3_bcv;
int sqlite3_bcv_open(const char* zModule, const char* zUser,
                     const char* zAuth, const char* zContainer,
                     sqlite3_bcv** ppOut);
void sqlite3_bcv_close(sqlite3_bcv*);
int sqlite3_bcv_create(sqlite3_bcv*, int szName, int szBlock);
int sqlite3_bcv_destroy(sqlite3_bcv*);
}

// ============================================================================
// Azurite helpers
// ============================================================================

#ifdef _WIN32
#define SQLITEX_POPEN _popen
#define SQLITEX_PCLOSE _pclose
#define SQLITEX_DEVNULL "NUL"
#else
#define SQLITEX_POPEN popen
#define SQLITEX_PCLOSE pclose
#define SQLITEX_DEVNULL "/dev/null"
#endif

bool is_azurite_running() {
    // curl exits 0 as soon as it gets any HTTP response (Azurite answers the
    // bare account URL with 400, which still means it is up); a non-zero exit
    // means the connection was refused or timed out. Checking the exit status
    // avoids shell-specific quoting of the -w format string across platforms.
    const std::string cmd =
        std::string("curl -s -o ") + SQLITEX_DEVNULL +
        " --connect-timeout 1 --max-time 2 \"http://" +
        std::string(kAzuriteHost) + ":" + std::to_string(kAzuritePort) + "/" +
        kAzuriteAccount + "\" 2>" + SQLITEX_DEVNULL;
    FILE* pipe = SQLITEX_POPEN(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return false;
    }
    std::array<char, 16> buf = {};
    while (fread(buf.data(), 1, buf.size(), pipe) > 0) {
        // Drain any output so the child process can exit cleanly.
    }
    return SQLITEX_PCLOSE(pipe) == 0;
}

bool create_container(const std::string& name) {
    sqlite3_bcv* bcv = nullptr;
    int rc = sqlite3_bcv_open(kStorage.c_str(), kAzuriteAccount, kAzuriteKey,
                               name.c_str(), &bcv);
    if (rc != 0 || bcv == nullptr) {
        return false;
    }
    rc = sqlite3_bcv_create(bcv, 16, 4194304);
    sqlite3_bcv_close(bcv);
    return rc == 0;
}

bool delete_container(const std::string& name) {
    sqlite3_bcv* bcv = nullptr;
    int const rc = sqlite3_bcv_open(kStorage.c_str(), kAzuriteAccount,
                                    kAzuriteKey, name.c_str(), &bcv);
    if (rc != 0 || bcv == nullptr) {
        return false;
    }
    sqlite3_bcv_destroy(bcv);
    sqlite3_bcv_close(bcv);
    return true;
}

std::string make_container_name(const std::string& test_name) {
    static int counter = 0;
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return std::string(kContainerPrefix) + "-" + test_name + "-" +
           std::to_string(now) + "-" + std::to_string(++counter);
}

std::string test_cache_path() {
    return (std::filesystem::temp_directory_path() / "bcvfs-int-cache").string();
}

// ============================================================================
// Test Fixture
// ============================================================================

class BcvfsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int setup_counter = 0;
        if (setup_counter++ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!is_azurite_running()) {
            GTEST_SKIP() << "Azurite not running on " << kAzuriteHost << ":"
                         << kAzuritePort;
        }

        std::string const test_name =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::string clean;
        for (char const c : test_name) {
            if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
                clean += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
        }
        container_name_ = make_container_name(clean.empty() ? "test" : clean);
        ASSERT_TRUE(create_container(container_name_))
            << "Failed to create Azurite container";

        cache_dir_ = test_cache_path() + "/" + container_name_;
        std::filesystem::create_directories(cache_dir_);
    }

    void TearDown() override {
        if (!container_name_.empty()) {
            delete_container(container_name_);
        }
        if (!cache_dir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(cache_dir_, ec);
        }
    }

    Bcvfs make_bcvfs(const std::string& vfs_name = kVfsName) {
        Bcvfs bcvfs(cache_dir_, vfs_name);
        bcvfs.set_cache_size(256LL * 1024 * 1024)
            .set_max_requests(4)
            .set_http_timeout(std::chrono::seconds(15));
        bcvfs.set_auth_callback(
            [](std::string_view, std::string_view, std::string_view,
               std::string& token) -> bool {
                token = kAzuriteKey;
                return true;
            });
        return bcvfs;
    }

    void attach_container(Bcvfs& bcvfs, const std::string& alias = "test") {
        bcvfs.attach(kStorage, kAzuriteAccount, container_name_, alias);
    }

protected:
    std::string container_name_;
    std::string cache_dir_;
};

// ============================================================================
// Test 1: Bcvfs construction, configuration, and attachment
// ============================================================================

TEST_F(BcvfsIntegrationTest, CreateConfigureAttach) {
    Bcvfs bcvfs = make_bcvfs();

    EXPECT_TRUE(bcvfs);
    EXPECT_NE(bcvfs.vfs_name().size(), 0U);
    EXPECT_FALSE(bcvfs.is_daemon());

    // Attach succeeds (manifest downloaded from emulator)
    EXPECT_NO_THROW(attach_container(bcvfs));
}

// ============================================================================
// Test 2: Attach with logging callback
// ============================================================================

TEST_F(BcvfsIntegrationTest, AttachWithLogging) {
    Bcvfs bcvfs = make_bcvfs();

    int log_count = 0;
    bcvfs.set_log_callback([&](std::string_view msg) {
        (void)msg;
        log_count++;
    });

    attach_container(bcvfs);
    // Log callback should have fired at least once during attach
    EXPECT_GE(log_count, 1);
}

// ============================================================================
// Test 3: Detach and reattach
// ============================================================================

TEST_F(BcvfsIntegrationTest, DetachAndReattach) {
    Bcvfs bcvfs = make_bcvfs();
    attach_container(bcvfs);

    EXPECT_NO_THROW(bcvfs.detach("test"));
    EXPECT_NO_THROW(attach_container(bcvfs));
}

// ============================================================================
// Test 4: Poll after attach
// ============================================================================

TEST_F(BcvfsIntegrationTest, PollAfterAttach) {
    Bcvfs bcvfs = make_bcvfs();
    attach_container(bcvfs);

    // Poll downloads fresh manifest — should succeed
    EXPECT_TRUE(bcvfs.poll("test"));
}

// ============================================================================
// Test 5: Multiple Bcvfs instances, same cache dir
// ============================================================================

TEST_F(BcvfsIntegrationTest, TwoInstancesSameCache) {
    Bcvfs bcvfs1 = make_bcvfs("bcvfs-a");
    attach_container(bcvfs1);

    // Second instance with different VFS name — must use a different
    // cache dir because bcVFS takes exclusive ownership of the directory.
    std::string const cache2 = cache_dir_ + "-2";
    std::filesystem::create_directories(cache2);
    Bcvfs bcvfs2(cache2, "bcvfs-b");
    bcvfs2.set_auth_callback(
        [](std::string_view, std::string_view, std::string_view,
           std::string& token) -> bool {
            token = kAzuriteKey;
            return true;
        });
    // Re-attach same container under different alias
    EXPECT_NO_THROW(
        bcvfs2.attach(kStorage, kAzuriteAccount, container_name_, "test2"));
    std::filesystem::remove_all(cache2);
}

// ============================================================================
// Test 6: Auth callback is invoked
// ============================================================================

TEST_F(BcvfsIntegrationTest, AuthCallbackInvoked) {
    int auth_count = 0;
    Bcvfs bcvfs(cache_dir_, kVfsName);
    bcvfs.set_auth_callback(
        [&](std::string_view storage, std::string_view account,
            std::string_view container, std::string& token) -> bool {
            auth_count++;
            EXPECT_EQ(storage, kStorage);
            EXPECT_EQ(account, kAzuriteAccount);
            EXPECT_EQ(container, container_name_);
            token = kAzuriteKey;
            return true;
        });

    bcvfs.attach(kStorage, kAzuriteAccount, container_name_, "test");
    EXPECT_EQ(auth_count, 1);
}

// ============================================================================
// Test 7: Configuration chain
// ============================================================================

TEST_F(BcvfsIntegrationTest, ConfigurationChain) {
    Bcvfs bcvfs(cache_dir_, kVfsName);
    // Configuration methods return *this for chaining
    bcvfs.set_cache_size(128LL * 1024 * 1024)
        .set_max_requests(2)
        .set_http_timeout(std::chrono::seconds(30));

    bcvfs.set_auth_callback([](std::string_view, std::string_view,
                                 std::string_view, std::string& token) -> bool {
        token = kAzuriteKey;
        return true;
    });

    attach_container(bcvfs);
    EXPECT_TRUE(bcvfs);
}

// ============================================================================
// Test 8: VFS name is preserved
// ============================================================================

TEST_F(BcvfsIntegrationTest, VfsNamePreserved) {
    Bcvfs bcvfs = make_bcvfs("my-custom-vfs");
    EXPECT_EQ(bcvfs.vfs_name(), "my-custom-vfs");

    attach_container(bcvfs);
    EXPECT_EQ(bcvfs.vfs_name(), "my-custom-vfs");
}

// ============================================================================
// Test 9: Bcvfs move semantics
// ============================================================================

TEST_F(BcvfsIntegrationTest, MoveSemantics) {
    Bcvfs bcvfs1 = make_bcvfs();
    attach_container(bcvfs1);
    EXPECT_TRUE(bcvfs1);

    Bcvfs const bcvfs2(std::move(bcvfs1));
    EXPECT_TRUE(bcvfs2);
    // Intentionally inspect the moved-from object: Bcvfs guarantees a valid,
    // empty state after move, which is exactly what this test verifies.
    EXPECT_FALSE(bcvfs1); // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
}

// ============================================================================
// Test 10: Is not daemon
// ============================================================================

TEST_F(BcvfsIntegrationTest, IsNotDaemon) {
    Bcvfs bcvfs = make_bcvfs();
    EXPECT_FALSE(bcvfs.is_daemon());
    attach_container(bcvfs);
    EXPECT_FALSE(bcvfs.is_daemon());
}

} // anonymous namespace
} // namespace sqlitex

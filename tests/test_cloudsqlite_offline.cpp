#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#ifdef SQLITEX_HAS_BCVFS

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sqlitex {
namespace {

// ============================================================================
// In-memory custom CloudSQLite storage backend + minimal loopback HTTP server
// ============================================================================

struct StoredObject {
    std::vector<uint8_t> data;
    std::string etag;
};

struct InMemoryStore {
    std::mutex mu;
    std::map<std::string, std::map<std::string, StoredObject>> containers;
    uint64_t next_etag = 1;
};

#ifndef _WIN32
class LoopbackHttpServer {
    public:
    explicit LoopbackHttpServer(std::shared_ptr<InMemoryStore> store)
        : store_(std::move(store)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::runtime_error("socket() failed");
        }
        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr),
                   sizeof(addr)) < 0) {
            ::close(listen_fd_);
            throw std::runtime_error("bind() failed");
        }
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        if (::listen(listen_fd_, 32) < 0) {
            ::close(listen_fd_);
            throw std::runtime_error("listen() failed");
        }
        thread_ = std::thread([this] { serve(); });
    }

    ~LoopbackHttpServer() {
        stop_.store(true);
        if (listen_fd_ >= 0) {
            ::shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    uint16_t port() const noexcept {
        return port_;
    }

    private:
    static bool send_all(int fd, const void* buf, size_t len) {
        const auto* p = static_cast<const char*>(buf);
        while (len > 0) {
            ssize_t n = ::send(fd, p, len, 0);
            if (n <= 0) {
                return false;
            }
            p += n;
            len -= static_cast<size_t>(n);
        }
        return true;
    }

    void handle_client(int fd) {
        std::string req;
        char buf[4096];
        size_t header_end = std::string::npos;
        while (header_end == std::string::npos) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                return;
            }
            req.append(buf, static_cast<size_t>(n));
            header_end = req.find("\r\n\r\n");
        }
        std::string header_block = req.substr(0, header_end);
        std::string body = req.substr(header_end + 4);

        // Parse request line
        auto line_end = header_block.find("\r\n");
        std::string req_line = header_block.substr(0, line_end);
        std::istringstream rl(req_line);
        std::string method;
        std::string path;
        rl >> method >> path;

        size_t content_len = 0;
        std::string if_match;
        std::string if_none_match;
        std::istringstream hs(line_end == std::string::npos
                                  ? ""
                                  : header_block.substr(line_end + 2));
        std::string hline;
        while (std::getline(hs, hline)) {
            if (!hline.empty() && hline.back() == '\r') {
                hline.pop_back();
            }
            auto colon = hline.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string k = hline.substr(0, colon);
            std::string v = hline.substr(colon + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
                v.erase(v.begin());
            }
            for (char& ch : k) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
            if (k == "content-length") {
                content_len = static_cast<size_t>(std::stoull(v));
            } else if (k == "if-match") {
                if_match = v;
            } else if (k == "if-none-match") {
                if_none_match = v;
            }
        }
        while (body.size() < content_len) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            body.append(buf, static_cast<size_t>(n));
        }

        // path = "/<container>/<key>"
        std::string container;
        std::string key;
        if (!path.empty() && path.front() == '/') {
            auto slash = path.find('/', 1);
            if (slash != std::string::npos) {
                container = path.substr(1, slash - 1);
                key = path.substr(slash + 1);
            }
        }

        int code = 200;
        std::string status_text = "OK";
        std::string resp_etag;
        std::vector<uint8_t> resp_body;

        {
            std::lock_guard<std::mutex> lock(store_->mu);
            auto cit = store_->containers.find(container);
            if (cit == store_->containers.end()) {
                code = 404;
                status_text = "Not Found";
            } else if (method == "GET") {
                auto oit = cit->second.find(key);
                if (oit == cit->second.end()) {
                    code = 404;
                    status_text = "Not Found";
                } else if (!if_none_match.empty() &&
                           if_none_match == oit->second.etag) {
                    code = 412;
                    status_text = "Precondition Failed";
                } else {
                    resp_etag = oit->second.etag;
                    resp_body = oit->second.data;
                }
            } else if (method == "PUT") {
                if (!if_match.empty()) {
                    auto oit = cit->second.find(key);
                    if (oit == cit->second.end() ||
                        oit->second.etag != if_match) {
                        code = 412;
                        status_text = "Precondition Failed";
                    }
                }
                if (code == 200) {
                    StoredObject obj;
                    obj.data.assign(
                        body.begin(),
                        body.begin() + static_cast<std::ptrdiff_t>(
                                           std::min(body.size(), content_len)));
                    obj.etag = "etag-" + std::to_string(store_->next_etag++);
                    resp_etag = obj.etag;
                    cit->second[key] = std::move(obj);
                }
            } else {
                code = 405;
                status_text = "Method Not Allowed";
            }
        }

        std::ostringstream out;
        out << "HTTP/1.1 " << code << " " << status_text << "\r\n";
        if (!resp_etag.empty()) {
            out << "ETag: " << resp_etag << "\r\n";
        }
        out << "X-Test-Backend: inmem\r\n";
        out << "Content-Length: " << resp_body.size() << "\r\n";
        out << "Connection: close\r\n\r\n";
        std::string hdr = out.str();
        send_all(fd, hdr.data(), hdr.size());
        if (!resp_body.empty()) {
            send_all(fd, resp_body.data(), resp_body.size());
        }
    }

    void serve() {
        while (!stop_.load()) {
            int cfd = ::accept(listen_fd_, nullptr, nullptr);
            if (cfd < 0) {
                if (stop_.load()) {
                    break;
                }
                continue;
            }
            handle_client(cfd);
            ::close(cfd);
        }
    }

    std::shared_ptr<InMemoryStore> store_;
    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
#endif // !_WIN32

class InMemoryContainer final : public BcvContainer {
    public:
    InMemoryContainer(std::shared_ptr<InMemoryStore> store,
                      std::string container_name, uint16_t http_port)
        : store_(std::move(store)),
          container_(std::move(container_name)),
          http_port_(http_port) {
    }

    void fetch(BcvJob& job, std::string_view file,
               BcvFetchConditions conditions,
               std::span<const std::byte> cond_data) override {
        if (http_port_ != 0) {
            BcvRequest req = job.create_request([](BcvJob& j, BcvRequest& r) {
                std::string msg;
                int st = r.status(&msg);
                if (st != SQLITE_OK) {
                    j.set_error(st, msg);
                    return;
                }
                if (auto et = r.header("ETag")) {
                    j.set_etag(*et);
                }
                auto raw_hdrs = r.raw_headers();
                if (!raw_hdrs.empty()) {
                    j.set_headers(raw_hdrs);
                }
                j.set_result(r.body());
            });
            std::string url = "http://127.0.0.1:" + std::to_string(http_port_) +
                              "/" + container_ + "/" + std::string(file);
            req.set_method(BcvHttpMethod::Get);
            req.set_uri(url);
            req.set_log("GET " + std::string(file));
            if (conditions.contains(BcvFetchCondition::ETag) &&
                !cond_data.empty()) {
                std::string hdr = "If-None-Match: ";
                hdr.append(reinterpret_cast<const char*>(cond_data.data()),
                           cond_data.size());
                req.set_header(hdr);
            }
            return;
        }
        std::lock_guard<std::mutex> lock(store_->mu);
        auto cit = store_->containers.find(container_);
        if (cit == store_->containers.end()) {
            job.set_error(404, "container not found");
            return;
        }
        auto oit = cit->second.find(std::string(file));
        if (oit == cit->second.end()) {
            job.set_error(404, "object not found");
            return;
        }
        job.set_etag(oit->second.etag);
        job.set_result(oit->second.data);
    }

    void put(BcvJob& job, std::string_view file,
             std::span<const std::byte> data, std::string_view etag) override {
        if (http_port_ != 0) {
            BcvRequest req = job.create_request([](BcvJob& j, BcvRequest& r) {
                std::string msg;
                int st = r.status(&msg);
                if (st != SQLITE_OK) {
                    j.set_error(st, msg);
                    return;
                }
                if (auto et = r.header("ETag")) {
                    j.set_etag(*et);
                }
            });
            std::string url = "http://127.0.0.1:" + std::to_string(http_port_) +
                              "/" + container_ + "/" + std::string(file);
            req.set_method(BcvHttpMethod::Put);
            req.set_uri(url);
            req.set_log("PUT " + std::string(file));
            req.set_header("Expect:");
            if (!etag.empty()) {
                req.set_header("If-Match: " + std::string(etag));
            }
            req.set_body(data);
            return;
        }
        std::lock_guard<std::mutex> lock(store_->mu);
        auto cit = store_->containers.find(container_);
        if (cit == store_->containers.end()) {
            job.set_error(404, "container not found");
            return;
        }
        auto key = std::string(file);
        if (!etag.empty()) {
            auto oit = cit->second.find(key);
            if (oit == cit->second.end() || oit->second.etag != etag) {
                job.set_error(412, "etag mismatch");
                return;
            }
        }
        StoredObject obj;
        if (!data.empty()) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
            obj.data.assign(bytes, bytes + data.size());
        }
        obj.etag = "etag-" + std::to_string(store_->next_etag++);
        std::string saved_etag = obj.etag;
        cit->second[std::move(key)] = std::move(obj);
        job.set_etag(saved_etag);
    }

    void remove(BcvJob& job, std::string_view file,
                std::string_view /*etag*/) override {
        std::lock_guard<std::mutex> lock(store_->mu);
        auto cit = store_->containers.find(container_);
        if (cit == store_->containers.end()) {
            job.set_error(404, "container not found");
            return;
        }
        cit->second.erase(std::string(file));
    }

    void list(BcvJob& job) override {
        std::lock_guard<std::mutex> lock(store_->mu);
        auto cit = store_->containers.find(container_);
        if (cit == store_->containers.end()) {
            job.set_error(404, "container not found");
            return;
        }
        std::string_view pfx = job.prefix();
        for (const auto& [name, obj] : cit->second) {
            (void)obj;
            if (pfx.empty() || name.rfind(pfx, 0) == 0) {
                job.add_list_entry(name);
            }
        }
    }

    void create_container(BcvJob& job) override {
        std::lock_guard<std::mutex> lock(store_->mu);
        auto [it, inserted] = store_->containers.emplace(
            container_, std::map<std::string, StoredObject>{});
        if (!inserted) {
            job.set_error(409, "container already exists");
        }
    }

    void destroy_container(BcvJob& job) override {
        std::lock_guard<std::mutex> lock(store_->mu);
        if (store_->containers.erase(container_) == 0) {
            job.set_error(404, "container not found");
        }
    }

    private:
    std::shared_ptr<InMemoryStore> store_;
    std::string container_;
    uint16_t http_port_ = 0;
};

std::filesystem::path make_temp_dir(const char* tag) {
    static int counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("sqlitex_bcv_" + std::string(tag) + "_" + std::to_string(now) +
                "_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);
    return dir;
}

TEST(CloudSqliteOfflineTest, ModuleBcvClientBcvfsAndPrefetchEndToEnd) {
    auto store = std::make_shared<InMemoryStore>();
    uint16_t port = 0;
#ifndef _WIN32
    LoopbackHttpServer server(store);
    port = server.port();
#endif
    std::string seen_param_val;

    register_bcv_module(
        "inmemtest",
        [store, port, &seen_param_val](
            std::span<const std::pair<std::string_view, std::string_view>>
                params,
            std::string_view /*user*/, std::string_view /*auth*/,
            std::string_view container) -> std::unique_ptr<BcvContainer> {
            for (const auto& [k, v] : params) {
                if (k == "tag") {
                    seen_param_val = std::string(v);
                }
            }
            return std::make_unique<InMemoryContainer>(
                store, std::string(container), port);
        });

    auto work_dir = make_temp_dir("e2e");
    auto local_src = work_dir / "src.db";
    auto local_dl = work_dir / "downloaded.db";
    auto cache_dir = work_dir / "cache";
    std::filesystem::create_directories(cache_dir);

    // 1. Create a local SQLite database to upload
    {
        Database db(local_src.string());
        db.execute("CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT);");
        db.execute(
            "INSERT INTO notes VALUES (1, 'hello cloud'), "
            "(2, 'offline module');");
    }

    // 2. Exercise BcvClient: create, upload, copy, download, remove, cleanup
    {
        BcvClient client("inmemtest?tag=ok", "testuser", "testauth", "bucket1");
        ASSERT_TRUE(client);
        EXPECT_EQ(seen_param_val, "ok");

        int progress_calls = 0;
        client.set_verbose(false)
            .set_max_requests(2)
            .set_log_level(0)
            .set_http_timeout(std::chrono::seconds(10))
            .set_find_orphans(true)
            .set_progress_callback([&](int64_t /*done*/, int64_t /*total*/) {
                ++progress_calls;
                return SQLITE_OK;
            })
            .set_log_callback([](std::string_view) {});

        client.create(16, 65536);
        client.upload(local_src.string(), "notes.db");
        EXPECT_GE(progress_calls, 1);

        client.copy("notes.db", "notes_copy.db");
        client.download("notes_copy.db", local_dl.string());

        {
            Database dl_db(local_dl.string());
            EXPECT_EQ(dl_db.query_value<int64_t>("SELECT COUNT(*) FROM notes;"),
                      2);
            EXPECT_EQ(dl_db.query_value<std::string>(
                          "SELECT body FROM notes WHERE id = 2;"),
                      "offline module");
        }

        client.remove("notes_copy.db");
        client.cleanup(std::chrono::seconds{0});
        EXPECT_EQ(client.errcode(), SQLITE_OK);

        // Downloading a missing DB throws Error and records errcode/errmsg
        EXPECT_THROW(client.download("does_not_exist.db",
                                     (work_dir / "missing.db").string()),
                     Error);
        EXPECT_NE(client.errcode(), SQLITE_OK);
        EXPECT_FALSE(client.errmsg().empty());

        // Clear callbacks cleanly
        client.set_progress_callback({});
        client.set_log_callback({});
    }

    // 3. Exercise Bcvfs + PrefetchHandle against the same in-memory container
    {
        Bcvfs bcvfs(cache_dir.string(), "bcvfs-offline-test");
        ASSERT_TRUE(bcvfs);
        EXPECT_NE(bcvfs.handle(), nullptr);
        EXPECT_FALSE(bcvfs.is_daemon());

        bcvfs.set_cache_size(32LL * 1024 * 1024)
            .set_max_requests(2)
            .set_http_timeout(std::chrono::seconds(10))
            .set_http_log_timeout(std::chrono::seconds(5))
            .set_http_log_max_entries(64);

        bcvfs.set_auth_callback([](std::string_view, std::string_view,
                                   std::string_view, std::string& token) {
            token = "testauth";
            return true;
        });

        bcvfs.set_masked_log_callback(
            [](BcvfsLogFlags /*mask*/, std::string_view /*msg*/) {},
            BcvfsLogFlag::Http | BcvfsLogFlag::Event);

        bcvfs.attach("inmemtest", "testuser", "bucket1", "bk",
                     BcvfsAttachFlag::None);

        // Prefetch notes.db via PrefetchHandle
        {
            PrefetchHandle pf = bcvfs.prefetch("bk", "notes.db");
            ASSERT_TRUE(pf);
            while (!pf.step(4, std::chrono::milliseconds(1000))) {
            }
            EXPECT_EQ(pf.status(), PrefetchStatus::Done);
            EXPECT_EQ(pf.error_code(), SQLITE_DONE);
            EXPECT_EQ(pf.outstanding_requests(), 0);
            EXPECT_GE(pf.on_demand_blocks(), 0);
        }

        // Open SQLite DB through the Bcvfs VFS, read & write, upload & poll
        {
            Database db("/bk/notes.db", OpenFlag::ReadWrite | OpenFlag::Create,
                        "bcvfs-offline-test");
            bcvfs.register_vtab(db);
            EXPECT_EQ(db.query_value<int64_t>("SELECT COUNT(*) FROM notes;"),
                      2);
            db.execute("INSERT INTO notes VALUES (3, 'written via bcvfs');");
        }

        EXPECT_EQ(bcvfs.upload("bk"), SQLITE_OK);
        EXPECT_TRUE(bcvfs.poll("bk"));

        // Exercise Bcvfs::copy, Bcvfs::remove, and Bcvfs::revert
        bcvfs.copy("bk", "notes.db", "notes_tmp.db");
        bcvfs.revert("bk");
        bcvfs.copy("bk", "notes.db", "notes_tmp2.db");
        bcvfs.remove("bk", "notes_tmp2.db");
        EXPECT_EQ(bcvfs.upload("bk"), SQLITE_OK);

        // Verify uploaded write is visible via fresh BcvClient download!
        {
            BcvClient verify("inmemtest", "testuser", "testauth", "bucket1");
            auto dl2 = work_dir / "verify.db";
            verify.download("notes.db", dl2.string());
            Database vdb(dl2.string());
            EXPECT_EQ(vdb.query_value<int64_t>("SELECT COUNT(*) FROM notes;"),
                      3);
            EXPECT_EQ(vdb.query_value<std::string>(
                          "SELECT body FROM notes WHERE id = 3;"),
                      "written via bcvfs");
        }

        bcvfs.detach("bk");
    }

    // 4. Destroy container cleanly
    {
        BcvClient client("inmemtest", "testuser", "testauth", "bucket1");
        client.destroy();
    }

    std::error_code ec;
    std::filesystem::remove_all(work_dir, ec);
}

} // namespace
} // namespace sqlitex

#endif // SQLITEX_HAS_BCVFS

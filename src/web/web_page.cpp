// SPDX-License-Identifier: AGPL-3.0-only
#include "web/web_page.h"
#include "web/web_bundle.h"
#include "melodee/web/card_files.h" // card_place, folder_json, free_path, file_type: Melodee's own, as on the MPC
#include "melodee/web/web_server.h" // json_text, safe_name
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <thread>
#include <vector>

#ifndef MELODEE_MOVE_VERSION
#define MELODEE_MOVE_VERSION "unknown"
#endif

namespace web {
namespace fs = std::filesystem;
namespace {

constexpr uint64_t upload_limit = uint64_t(4) << 30; // a sample or project, not a disk image
constexpr uint64_t room_margin = uint64_t(64) << 20;  // left free on /data for the Move itself
constexpr auto frame_gap = std::chrono::milliseconds(33); // 30 frames a second at most, a stream
constexpr auto keep_alive = std::chrono::seconds(15);
const char* cookie_name = "melodee_move";

Reply ok(const std::string& json) {
    Reply reply;
    reply.body = json;
    return reply;
}

std::string query_of(const Request& request, const char* key) {
    auto found = request.query.find(key);
    return found == request.query.end() ? std::string() : found->second;
}

std::string cookie_of(const Request& request) {
    auto found = request.headers.find("cookie");
    if (found == request.headers.end()) return {};
    const std::string& all = found->second;
    const std::string key = std::string(cookie_name) + "=";
    for (size_t at = 0; at < all.size();) {
        while (at < all.size() && (all[at] == ' ' || all[at] == ';')) ++at;
        size_t end = all.find(';', at);
        if (end == std::string::npos) end = all.size();
        if (all.compare(at, key.size(), key) == 0) return all.substr(at + key.size(), end - at - key.size());
        at = end;
    }
    return {};
}

const char* via_word(Via via) { return via == Via::Usb ? "usb" : via == Via::Local ? "local" : "wifi"; }

std::string base64(const uint8_t* data, size_t size) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t n = 0; n < size; n += 3) {
        uint32_t v = uint32_t(data[n]) << 16 | (n + 1 < size ? uint32_t(data[n + 1]) << 8 : 0) | (n + 2 < size ? data[n + 2] : 0);
        out += table[v >> 18 & 63];
        out += table[v >> 12 & 63];
        out += n + 1 < size ? table[v >> 6 & 63] : '=';
        out += n + 2 < size ? table[v & 63] : '=';
    }
    return out;
}

// The nearest folder of `path` that exists: for free space before Melodee's folder is made.
fs::path existing(fs::path path) {
    std::error_code error;
    while (!path.empty() && !fs::exists(path, error)) path = path.parent_path();
    return path.empty() ? fs::path("/") : path;
}

// The request's body into `path`, flushed. False, with nothing left behind, when it stopped short.
bool receive(const Body& body, uint64_t length, const fs::path& path) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    std::vector<char> buffer(256 * 1024);
    uint64_t written = 0;
    bool good = true;
    while (good && written < length) {
        long got = body(buffer.data(), buffer.size());
        if (got <= 0) { good = false; break; }
        for (long at = 0; at < got;) {
            ssize_t n = ::write(fd, buffer.data() + at, size_t(got - at));
            if (n <= 0) { good = false; break; }
            at += n;
        }
        written += uint64_t(got);
    }
    good = good && ::fsync(fd) == 0;
    good = ::close(fd) == 0 && good;
    std::error_code error;
    if (!good) fs::remove(path, error);
    return good;
}

} // namespace

const char* version() { return MELODEE_MOVE_VERSION; }

// ---- Frames ----

void Frames::publish(const move::Frame& next) {
    {
        std::lock_guard<std::mutex> held(lock);
        if (published.load() && next == frame) return;
        frame = next;
        published.fetch_add(1);
    }
    changed.notify_all();
}

bool Frames::next(uint64_t& seen, move::Frame& out, std::chrono::milliseconds limit) {
    std::unique_lock<std::mutex> held(lock);
    if (!changed.wait_for(held, limit, [&] { return closing || published.load() != seen; }) || closing) return false;
    seen = published.load();
    out = frame;
    return true;
}

void Frames::close() {
    {
        std::lock_guard<std::mutex> held(lock);
        closing = true;
    }
    changed.notify_all();
}

// ---- The page ----

WebPage::WebPage(PageOptions given) : options(std::move(given)) {}

bool WebPage::start(std::string& error) {
    return server.start(options.port, options.tries, [this](const Request& r, const Body& b) { return handle(r, b); }, error);
}

void WebPage::stop() {
    screen.close();
    server.stop();
}

Reply WebPage::handle(const Request& request, const Body& body) {
    const bool get = request.method == "GET" || request.method == "HEAD", post = request.method == "POST";
    const std::string& path = request.path;
    if (get && path.compare(0, 5, "/api/") != 0) {
        if (auto file = bundled(path)) {
            Reply reply;
            reply.type = file.type;
            reply.body.assign(file.data, file.size);
            return reply;
        }
        if (path == "/favicon.ico") return {204, "image/x-icon", "", "", "", {}, {}}; // the page names its own icon
        return error_reply(404, "Not found");
    }
    const Via via = Network::now().via(request.local, request.peer);
    if (path == "/api/pair" && get) return pair_state(request, via);
    if (path == "/api/pair/ask" && post) return pair_ask(request, via);
    if (path == "/api/pair" && post) return pair_try(request, via);
    // Everything else: on the USB cable, from the Move itself, or a paired browser.
    if (via == Via::Other && !pair.allowed(cookie_of(request), request.peer)) {
        Reply reply = error_reply(401, "Pair this browser first");
        reply.body = "{\"error\":\"Pair this browser first\",\"pair\":true}";
        return reply;
    }
    if (get && path == "/api/status") return status(request, via);
    if (get && path == "/api/screen") return stream_screen();
    if (get && path == "/api/list") return list(request);
    if (get && path == "/api/file") return send_file(request);
    if (post && path == "/api/upload") return upload(request, body);
    if (post && path == "/api/mkdir") return make_folder(request);
    if (post && path == "/api/rename") return rename(request);
    if (post && path == "/api/delete") return remove(request);
    return error_reply(404, "Not found");
}

Reply WebPage::status(const Request&, Via via) {
    std::error_code error;
    auto space = fs::space(existing(options.home), error);
    auto network = Network::now();
    return ok("{\"version\":" + melodee::web::json_text(version()) + ",\"via\":\"" + via_word(via) + "\""
        + ",\"home\":" + melodee::web::json_text(options.home)
        + ",\"free\":" + std::to_string(error ? 0 : space.available) + ",\"total\":" + std::to_string(error ? 0 : space.capacity)
        + ",\"port\":" + std::to_string(port()) + ",\"usb\":" + melodee::web::json_text(network.usb())
        + ",\"wifi\":" + melodee::web::json_text(network.wifi()) + "}");
}

// ---- Pairing ----

Reply WebPage::pair_state(const Request& request, Via via) {
    if (via != Via::Other) return ok("{\"state\":\"open\",\"seconds\":0}");
    unsigned seconds = 0;
    std::string state = pair.state(request.peer, Pairing::Clock::now(), seconds);
    if (pair.allowed(cookie_of(request), request.peer)) state = "paired";
    return ok("{\"state\":\"" + state + "\",\"seconds\":" + std::to_string(seconds) + "}");
}

Reply WebPage::pair_ask(const Request& request, Via via) {
    if (via != Via::Other) return ok("{\"open\":true,\"seconds\":0}");
    auto asked = pair.ask(request.peer, Pairing::Clock::now());
    if (asked.status != 200) return error_reply(asked.status, asked.error);
    std::fprintf(stderr, "melodee-move: web: %s asks to pair; the Move shows a code\n", request.peer.c_str());
    return ok("{\"seconds\":" + std::to_string(asked.seconds) + "}");
}

Reply WebPage::pair_try(const Request& request, Via via) {
    if (via != Via::Other) return ok("{\"open\":true}");
    auto tried = pair.attempt(request.peer, query_of(request, "code"), Pairing::Clock::now());
    if (tried.status == 403) {
        Reply reply = error_reply(403, tried.error);
        reply.body = "{\"error\":" + melodee::web::json_text(tried.error) + ",\"left\":" + std::to_string(tried.left) + "}";
        return reply;
    }
    if (tried.status != 200) return error_reply(tried.status, tried.error);
    std::fprintf(stderr, "melodee-move: web: %s paired\n", request.peer.c_str());
    Reply reply = ok("{}");
    // Until melodee-move stops: the token lives in its memory only.
    reply.headers.push_back({"Set-Cookie", std::string(cookie_name) + "=" + tried.token + "; Path=/; HttpOnly; SameSite=Strict"});
    return reply;
}

// ---- Screen ----

Reply WebPage::stream_screen() {
    Reply reply;
    reply.type = "text/event-stream";
    reply.stream = [this](const Send& send) {
        using Clock = std::chrono::steady_clock;
        uint64_t seen = 0;
        move::Frame frame;
        auto last = Clock::now();
        if (!send("retry: 2000\n\n")) return;
        while (!server.stopping()) {
            if (screen.next(seen, frame, std::chrono::milliseconds(1000))) {
                if (!send("data: " + base64(frame.data(), frame.size()) + "\n\n")) return;
                last = Clock::now();
                std::this_thread::sleep_for(frame_gap); // frames in between are passed over: the newest goes next
            } else if (Clock::now() - last >= keep_alive) {
                if (!send(": still here\n\n")) return;
                last = Clock::now();
            }
        }
    };
    return reply;
}

// ---- Files: Melodee's folder ----

bool WebPage::place(const std::string& rel, std::string& out) const {
    fs::path where;
    std::string why;
    if (!melodee::web::card_place(options.home, rel, where, why)) return false;
    out = where.string();
    return true;
}

void WebPage::adopt(const std::string& path) const {
    if (::geteuid() != 0) return;
    // Run over SSH, melodee-move is root; what it makes goes to whoever owns
    // the folder Melodee's sits in (the Move's user), so a run from Schwung's
    // Tools menu, as that user, may change it again.
    struct stat owner {};
    if (::stat(fs::path(options.home).parent_path().c_str(), &owner) != 0) return;
    [[maybe_unused]] int ignored = ::lchown(path.c_str(), owner.st_uid, owner.st_gid);
}

Reply WebPage::list(const Request& request) {
    const std::string rel = query_of(request, "path");
    std::string folder;
    if (!place(rel, folder)) return error_reply(404, "Not in Melodee's folder");
    std::error_code error;
    auto space = fs::space(existing(options.home), error);
    std::string entries = "[]";
    if (fs::is_directory(folder, error)) entries = melodee::web::folder_json(folder);
    else if (!rel.empty()) return error_reply(404, "Not a folder"); // Melodee's own folder is there once something is saved
    return ok("{\"path\":" + melodee::web::json_text(rel) + ",\"home\":" + melodee::web::json_text(options.home)
        + ",\"free\":" + std::to_string(error ? 0 : space.available) + ",\"entries\":" + entries + "}");
}

Reply WebPage::send_file(const Request& request) {
    std::string file;
    if (!place(query_of(request, "path"), file)) return error_reply(404, "Not in Melodee's folder");
    Reply reply;
    reply.type = melodee::web::file_type(fs::path(file).filename().string());
    reply.file = file;
    if (query_of(request, "save") == "1") reply.download = fs::path(file).filename().string(); // else played in place
    return reply;
}

Reply WebPage::upload(const Request& request, const Body& body) {
    std::string folder;
    if (!place(query_of(request, "path"), folder)) return error_reply(404, "Not in Melodee's folder");
    auto name = melodee::web::safe_name(query_of(request, "name"), "");
    if (name.empty()) return error_reply(400, "Not a name the Move takes");
    if (request.length > upload_limit) return error_reply(413, "Too big (4 GB at most)");
    std::error_code error;
    if (query_of(request, "path").empty() && !fs::exists(folder, error)) { // the first file: Melodee's folder is made now
        fs::create_directories(folder, error);
        adopt(folder);
    }
    if (!fs::is_directory(folder, error)) return error_reply(404, "Not a folder");
    auto space = fs::space(folder, error);
    if (error || space.available < request.length + room_margin) return error_reply(507, "The Move is full");
    // Hidden while it comes in, so Load and the page pass it by; a name of its own for each upload at once.
    const fs::path temp = fs::path(folder) / (".melodee-upload-" + std::to_string(uploads.fetch_add(1)) + ".part");
    if (!receive(body, request.length, temp)) return error_reply(400, "The upload stopped before the end");
    const fs::path to = melodee::web::free_path(folder, name); // never over a file of the same name: "Kick 2.wav"
    fs::rename(temp, to, error);
    if (error) { fs::remove(temp, error); return error_reply(500, "Could not write on the Move"); }
    adopt(to.string());
    files_changed.store(true);
    return ok("{\"name\":" + melodee::web::json_text(to.filename().string()) + "}");
}

Reply WebPage::make_folder(const Request& request) {
    std::string folder;
    if (!place(query_of(request, "path"), folder)) return error_reply(404, "Not in Melodee's folder");
    auto name = melodee::web::safe_name(query_of(request, "name"), "");
    if (name.empty()) return error_reply(400, "Not a name the Move takes");
    std::error_code error;
    const fs::path made = fs::path(folder) / name;
    if (fs::exists(made, error)) return error_reply(409, "Already there");
    bool home_made = !fs::exists(folder, error) && query_of(request, "path").empty() && fs::create_directories(folder, error);
    if (home_made) adopt(folder);
    if (!fs::create_directory(made, error) || error) return error_reply(500, "Could not write on the Move");
    adopt(made.string());
    files_changed.store(true);
    return ok("{\"name\":" + melodee::web::json_text(name) + "}");
}

Reply WebPage::rename(const Request& request) {
    const std::string rel = query_of(request, "path");
    std::string from;
    if (rel.empty() || !place(rel, from)) return error_reply(403, "This one stays");
    auto to_name = melodee::web::safe_name(query_of(request, "to"), "");
    if (to_name.empty()) return error_reply(400, "Not a name the Move takes");
    std::error_code error;
    if (!fs::exists(fs::symlink_status(from, error))) return error_reply(404, "Not found");
    const fs::path to = fs::path(from).parent_path() / to_name; // in the same folder: a name, not a move
    if (fs::exists(fs::symlink_status(to, error))) return error_reply(409, "Already there");
    fs::rename(from, to, error);
    if (error) return error_reply(500, "Could not rename it");
    ::sync();
    files_changed.store(true);
    return ok("{\"name\":" + melodee::web::json_text(to_name) + "}");
}

Reply WebPage::remove(const Request& request) {
    const std::string rel = query_of(request, "path");
    std::string target;
    if (rel.empty() || !place(rel, target)) return error_reply(403, "This one stays"); // Melodee's folder itself stays
    std::error_code error;
    if (!fs::exists(fs::symlink_status(target, error))) return error_reply(404, "Not found");
    fs::remove_all(target, error);
    if (error) return error_reply(500, "Could not delete it");
    ::sync();
    files_changed.store(true);
    return ok("{}");
}

} // namespace web

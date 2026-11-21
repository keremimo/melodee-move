// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// A small HTTP/1.1 server for Melodee's web page on the Move (web_page.h).
// Melodee's page on the MPC (melodee-sample, web/web_server.h) answers one
// request at a time; here the screen is a stream that stays open, so each
// connection has a thread of its own, at most `max_connections` at once,
// each closed after its reply. None of them is the audio thread, and none
// is realtime.
//
// What it refuses before a handler sees the request, as the MPC's does:
// - a head over 16 KB, a bad request line, or a body sent chunked (411)
// - a Host that is not an IP address, localhost or a .local name: a web
//   site whose name points at the Move's address (DNS rebinding) gets
//   nothing (421)
// - a POST without an `X-Melodee` header: a page on another site cannot
//   add it without a CORS preflight, and OPTIONS is refused (405)
namespace web {

struct Request {
    std::string method, path;                   // "POST", "/api/upload"
    std::map<std::string, std::string> query;   // decoded ?name=value pairs
    std::map<std::string, std::string> headers; // names in lower case
    uint64_t length = 0;                        // Content-Length; 0 without one
    // The connection's two ends, IPv4 in dots: the Move's address it came
    // in on, and the browser's.
    std::string local, peer;
};

// Sends part of a streamed reply; false once the browser has gone.
using Send = std::function<bool(const std::string&)>;

struct Reply {
    int status = 200;
    std::string type = "application/json; charset=utf-8";
    std::string body;
    // A file sent as the body instead, read as it goes out; `download` names it for a save.
    std::string file, download;
    std::vector<std::pair<std::string, std::string>> headers; // more, such as Set-Cookie
    // A reply that goes on (the screen, as server-sent events): called once
    // the head is out, on the connection's thread, until it returns.
    std::function<void(const Send&)> stream;
};

// Reads up to `size` bytes of the request's body into `into`: the bytes
// read, 0 once all `length` are read, -1 when the browser stopped sending.
using Body = std::function<long(char* into, size_t size)>;
using Handler = std::function<Reply(const Request&, const Body&)>;

Reply error_reply(int status, const std::string& words);

class Server {
public:
    static constexpr unsigned max_connections = 8;
    ~Server() { stop(); }
    // Listens on every IPv4 address at the first free port of `first`,
    // `first` + 1 ... (`tries` in all; `first` 0: any free port, for tests)
    // and serves on new threads. False with `error` when none will open.
    bool start(uint16_t first, unsigned tries, Handler handler, std::string& error);
    // Ends every connection, streams too, and waits for their threads.
    void stop();
    uint16_t port() const { return bound; }
    bool stopping() const { return closing.load(); }

private:
    void run();
    void serve(int client);
    int listener = -1;
    uint16_t bound = 0;
    Handler handler;
    std::atomic<bool> closing {false};
    std::thread thread;
    std::mutex lock; // the connections
    std::condition_variable idle;
    std::set<int> clients;
};

} // namespace web

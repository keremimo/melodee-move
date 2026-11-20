// SPDX-License-Identifier: AGPL-3.0-only
#include "web/http.h"
#include "melodee/web/web_server.h" // json_text, url_decode, host_allowed: Melodee's own, as on the MPC
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace web {
namespace {
constexpr size_t head_limit = 16 * 1024;

const char* status_text(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 413: return "Content Too Large";
    case 421: return "Misdirected Request";
    case 429: return "Too Many Requests";
    case 503: return "Service Unavailable";
    case 507: return "Insufficient Storage";
    default: return status >= 500 ? "Internal Server Error" : "Error";
    }
}

bool send_all(int fd, const char* data, size_t size) {
    while (size) {
        ssize_t sent = ::send(fd, data, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        data += sent;
        size -= size_t(sent);
    }
    return true;
}

// A name for Content-Disposition: plain ASCII with quotes and controls taken out, and the whole name UTF-8 encoded.
std::string disposition(const std::string& name) {
    std::string plain, encoded;
    for (unsigned char c : name) {
        plain += (c < 0x20 || c >= 0x7f || c == '"' || c == '\\') ? '_' : char(c);
        if (std::isalnum(c) || std::strchr("-._~", c)) encoded += char(c);
        else { char hex[4]; std::snprintf(hex, sizeof(hex), "%%%02X", c); encoded += hex; }
    }
    return "attachment; filename=\"" + plain + "\"; filename*=UTF-8''" + encoded;
}

std::string head_of(const Reply& reply, const std::string& length) {
    std::string out = "HTTP/1.1 " + std::to_string(reply.status) + " " + status_text(reply.status) + "\r\n"
        + "Content-Type: " + reply.type + "\r\n";
    if (!length.empty()) out += "Content-Length: " + length + "\r\n";
    if (!reply.download.empty()) out += "Content-Disposition: " + disposition(reply.download) + "\r\n";
    for (const auto& [name, value] : reply.headers) out += name + ": " + value + "\r\n";
    return out + "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Connection: close\r\n\r\n";
}

void send_reply(int fd, const Reply& reply, bool head_only) {
    if (reply.stream) {
        std::string head = head_of(reply, {});
        if (!send_all(fd, head.data(), head.size()) || head_only) return;
        reply.stream([fd](const std::string& part) { return send_all(fd, part.data(), part.size()); });
        return;
    }
    int file = -1;
    uint64_t length = reply.body.size();
    if (!reply.file.empty()) {
        file = ::open(reply.file.c_str(), O_RDONLY | O_CLOEXEC);
        struct stat info {};
        if (file < 0 || ::fstat(file, &info) != 0 || !S_ISREG(info.st_mode)) {
            if (file >= 0) ::close(file);
            return send_reply(fd, error_reply(404, "Not found"), head_only);
        }
        length = uint64_t(info.st_size);
    }
    std::string out = head_of(reply, std::to_string(length));
    if (!head_only && file < 0) out += reply.body;
    bool ok = send_all(fd, out.data(), out.size());
    if (file >= 0) {
        for (off_t at = 0; ok && !head_only && uint64_t(at) < length;) {
            ssize_t sent = ::sendfile(fd, file, &at, size_t(std::min<uint64_t>(length - uint64_t(at), 1 << 20)));
            if (sent < 0 && errno == EINTR) continue;
            ok = sent > 0;
        }
        ::close(file);
    }
}

std::string lowered(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trimmed(const std::string& text) {
    size_t from = text.find_first_not_of(" \t"), to = text.find_last_not_of(" \t");
    return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
}

std::map<std::string, std::string> parse_query(const std::string& query) {
    std::map<std::string, std::string> out;
    size_t at = 0;
    while (at < query.size()) {
        size_t end = query.find('&', at);
        if (end == std::string::npos) end = query.size();
        std::string pair = query.substr(at, end - at);
        size_t eq = pair.find('=');
        if (!pair.empty())
            out[melodee::web::url_decode(pair.substr(0, eq))] = eq == std::string::npos ? std::string() : melodee::web::url_decode(pair.substr(eq + 1));
        at = end + 1;
    }
    return out;
}

std::string address_of(const sockaddr_in& address) {
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text));
    return text;
}
} // namespace

Reply error_reply(int status, const std::string& words) {
    Reply reply;
    reply.status = status;
    reply.body = "{\"error\":" + melodee::web::json_text(words) + "}";
    return reply;
}

bool Server::start(uint16_t first, unsigned tries, Handler serve_with, std::string& error) {
    stop();
    error = "no port";
    for (unsigned n = 0; n < std::max(1u, tries); ++n) {
        uint16_t port = first ? uint16_t(first + n) : 0;
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) { error = std::strerror(errno); return false; }
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);
        socklen_t size = sizeof(address);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && ::listen(fd, 16) == 0
            && getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0) {
            listener = fd;
            bound = ntohs(address.sin_port);
            break;
        }
        error = "port " + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        if (!first) break;
    }
    if (listener < 0) return false;
    error.clear();
    handler = std::move(serve_with);
    closing.store(false);
    thread = std::thread([this] { run(); });
    return true;
}

void Server::stop() {
    closing.store(true);
    if (thread.joinable()) thread.join();
    if (listener >= 0) ::close(listener);
    listener = -1;
    std::unique_lock<std::mutex> held(lock);
    for (int fd : clients) ::shutdown(fd, SHUT_RDWR); // a wait in recv or send ends at once; a stream sees it fail
    idle.wait(held, [this] { return clients.empty(); });
}

void Server::run() {
    while (!closing.load()) {
        pollfd wait {listener, POLLIN, 0};
        if (::poll(&wait, 1, 250) <= 0) continue; // a quarter second at most before stop() is seen
        int client = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) continue;
        timeval limit {15, 0}; // a browser that goes quiet mid-upload lets go of its thread after 15 s
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
        {
            std::lock_guard<std::mutex> held(lock);
            if (clients.size() >= max_connections) {
                send_reply(client, error_reply(503, "The Move is busy; try again"), false);
                ::close(client);
                continue;
            }
            clients.insert(client);
        }
        std::thread([this, client] {
            serve(client);
            // Done sending. Read what the browser still sends for a moment (a body the reply came before), so it
            // reads the reply instead of a reset; it closes its side once it has the reply.
            ::shutdown(client, SHUT_WR);
            timeval brief {2, 0};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &brief, sizeof(brief));
            char rest[4096];
            for (int n = 0; n < 256 && !closing.load() && ::recv(client, rest, sizeof(rest), 0) > 0; ++n) {}
            std::lock_guard<std::mutex> held(lock);
            clients.erase(client);
            ::close(client);
            idle.notify_all();
        }).detach();
    }
}

void Server::serve(int client) {
    // The head: the request line and headers, up to the blank line. What came after it is the body's start.
    std::string head;
    size_t end = std::string::npos;
    char chunk[4096];
    while (end == std::string::npos) {
        if (head.size() > head_limit) return send_reply(client, error_reply(400, "Request too large"), false);
        ssize_t got = ::recv(client, chunk, sizeof(chunk), 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return;
        head.append(chunk, size_t(got));
        end = head.find("\r\n\r\n");
    }
    if (end > head_limit) return send_reply(client, error_reply(400, "Request too large"), false);
    std::string extra = head.substr(end + 4);
    head.resize(end);

    Request request;
    sockaddr_in local {}, peer {};
    socklen_t size = sizeof(local);
    if (getsockname(client, reinterpret_cast<sockaddr*>(&local), &size) == 0) request.local = address_of(local);
    size = sizeof(peer);
    if (getpeername(client, reinterpret_cast<sockaddr*>(&peer), &size) == 0) request.peer = address_of(peer);

    size_t line_end = head.find("\r\n");
    std::string line = head.substr(0, line_end);
    size_t a = line.find(' '), b = line.rfind(' ');
    if (a == std::string::npos || b == a || line.compare(b + 1, 5, "HTTP/") != 0)
        return send_reply(client, error_reply(400, "Bad request"), false);
    request.method = line.substr(0, a);
    std::string target = line.substr(a + 1, b - a - 1);
    size_t mark = target.find('?');
    request.path = melodee::web::url_decode(target.substr(0, mark));
    if (mark != std::string::npos) request.query = parse_query(target.substr(mark + 1));
    size_t at = line_end == std::string::npos ? head.size() : line_end + 2;
    while (at < head.size()) {
        size_t next = head.find("\r\n", at);
        if (next == std::string::npos) next = head.size();
        std::string field = head.substr(at, next - at);
        size_t colon = field.find(':');
        if (colon != std::string::npos) request.headers[lowered(trimmed(field.substr(0, colon)))] = trimmed(field.substr(colon + 1));
        at = next + 2;
    }

    const bool head_only = request.method == "HEAD";
    if (!melodee::web::host_allowed(request.headers["host"]))
        return send_reply(client, error_reply(421, "Open this page by the Move's address"), head_only);
    if (request.method != "GET" && request.method != "HEAD" && request.method != "POST")
        return send_reply(client, error_reply(405, "Not allowed"), head_only);
    if (request.method == "POST") {
        if (!request.headers.count("x-melodee")) return send_reply(client, error_reply(403, "Not from Melodee's page"), false);
        if (request.headers.count("transfer-encoding")) return send_reply(client, error_reply(411, "Send a length"), false);
        auto length = request.headers.find("content-length");
        if (length == request.headers.end()) request.length = 0; // a POST with no body (a fetch() without one sends 0)
        else if (length->second.empty() || length->second.size() > 18
                 || !std::all_of(length->second.begin(), length->second.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return send_reply(client, error_reply(411, "Send a length"), false);
        else request.length = std::strtoull(length->second.c_str(), nullptr, 10);
    }

    uint64_t left = request.length;
    Body body = [&](char* into, size_t size) -> long {
        if (left == 0) return 0;
        size = size_t(std::min<uint64_t>(std::min<size_t>(size, 1 << 20), left));
        if (!extra.empty()) {
            size_t n = std::min(size, extra.size());
            std::memcpy(into, extra.data(), n);
            extra.erase(0, n);
            left -= n;
            return long(n);
        }
        for (;;) {
            ssize_t got = ::recv(client, into, size, 0);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return -1;
            left -= uint64_t(got);
            return long(got);
        }
    };
    Reply reply;
    try {
        reply = handler(request, body);
    } catch (const std::exception& e) {
        reply = error_reply(500, e.what());
    }
    send_reply(client, reply, head_only);
}

} // namespace web

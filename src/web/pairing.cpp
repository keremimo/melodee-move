// SPDX-License-Identifier: AGPL-3.0-only
#include "web/pairing.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace web {
namespace {

bool parse(const std::string& text, uint32_t& ip) {
    in_addr address {};
    if (inet_pton(AF_INET, text.c_str(), &address) != 1) return false;
    ip = ntohl(address.s_addr);
    return true;
}

bool usb_name(const std::string& name) { return name.compare(0, 3, "usb") == 0; }

void urandom(uint8_t* into, size_t size) {
    std::ifstream source("/dev/urandom", std::ios::binary);
    if (!source.read(reinterpret_cast<char*>(into), std::streamsize(size))) throw std::runtime_error("no /dev/urandom");
}

// The same time whether the first digit or the last is wrong.
bool same(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char differ = 0;
    for (size_t n = 0; n < a.size(); ++n) differ |= static_cast<unsigned char>(a[n] ^ b[n]);
    return differ == 0;
}

unsigned seconds_until(Pairing::Clock::time_point end, Pairing::Clock::time_point now) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
    return ms <= 0 ? 0 : unsigned((ms + 999) / 1000);
}

} // namespace

Network Network::now() {
    Network network;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return network;
    for (auto* at = list; at; at = at->ifa_next) {
        if (!at->ifa_addr || at->ifa_addr->sa_family != AF_INET || !(at->ifa_flags & IFF_UP)) continue;
        Interface one;
        one.name = at->ifa_name;
        one.ip = ntohl(reinterpret_cast<sockaddr_in*>(at->ifa_addr)->sin_addr.s_addr);
        one.mask = at->ifa_netmask ? ntohl(reinterpret_cast<sockaddr_in*>(at->ifa_netmask)->sin_addr.s_addr) : 0xffffffffu;
        char text[INET_ADDRSTRLEN] = {};
        in_addr address {htonl(one.ip)};
        inet_ntop(AF_INET, &address, text, sizeof(text));
        one.address = text;
        network.interfaces.push_back(one);
    }
    freeifaddrs(list);
    return network;
}

Via Network::via(const std::string& local, const std::string& peer) const {
    uint32_t here = 0, there = 0;
    if (!parse(local, here) || !parse(peer, there)) return Via::Other;
    if ((here >> 24) == 127 && (there >> 24) == 127) return Via::Local;
    for (const auto& one : interfaces)
        if (usb_name(one.name) && one.ip == here && (there & one.mask) == (one.ip & one.mask)) return Via::Usb;
    return Via::Other;
}

std::string Network::usb() const {
    for (const auto& one : interfaces)
        if (usb_name(one.name)) return one.address;
    return {};
}

std::string Network::wifi() const {
    for (const auto& one : interfaces)
        if (!usb_name(one.name) && (one.ip >> 24) != 127) return one.address;
    return {};
}

Pairing::Pairing(std::function<void(uint8_t*, size_t)> source) : random(source ? std::move(source) : urandom) {}

std::string Pairing::fresh_code() {
    // Six digits, each code as likely as any other: values past the last
    // whole million of 2^32 are drawn again.
    for (;;) {
        uint8_t b[4];
        random(b, sizeof(b));
        uint32_t value = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
        if (value >= 4294000000u) continue;
        char text[8];
        std::snprintf(text, sizeof(text), "%06u", value % 1000000u);
        return text;
    }
}

std::string Pairing::fresh_token() {
    uint8_t b[16];
    random(b, sizeof(b));
    std::string out;
    for (uint8_t x : b) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", x);
        out += hex;
    }
    return out;
}

void Pairing::expire(Clock::time_point now) const {
    if (code.empty() || now < ends) return;
    outcome = Shown::Kind::Refused;
    outcome_from = asker;
    outcome_why = "TIME UP";
    outcome_at = ends;
    code.clear();
}

Pairing::Asked Pairing::ask(const std::string& from, Clock::time_point now) {
    std::lock_guard<std::mutex> held(lock);
    expire(now);
    if (!code.empty() && asker != from)
        return {429, 0, "Another browser is pairing; try again in " + std::to_string(seconds_until(ends, now)) + " seconds"};
    if (last_ask != Clock::time_point {} && now - last_ask < ask_gap) return {429, 0, "Wait a moment before asking again"};
    if (auto found = paused.find(from); found != paused.end()) {
        if (now < found->second)
            return {429, 0, "Refused on the Move; ask again in " + std::to_string(seconds_until(found->second, now) / 60 + 1) + " minutes"};
        paused.erase(found);
    }
    code = fresh_code();
    asker = from;
    ends = now + code_life;
    left = tries;
    last_ask = now;
    outcome = Shown::Kind::Nothing;
    return {200, unsigned(std::chrono::duration_cast<std::chrono::seconds>(code_life).count()), {}};
}

Pairing::Tried Pairing::attempt(const std::string& from, const std::string& typed, Clock::time_point now) {
    std::lock_guard<std::mutex> held(lock);
    expire(now);
    if (code.empty() || asker != from) return {410, 0, {}, "No code is waiting for this browser; ask the Move for one"};
    if (same(typed, code)) {
        Tried out {200, 0, fresh_token(), {}};
        tokens.push_back({out.token, from});
        if (tokens.size() > most_paired) tokens.erase(tokens.begin());
        outcome = Shown::Kind::Paired;
        outcome_from = from;
        outcome_why.clear();
        outcome_at = now;
        code.clear();
        return out;
    }
    if (--left == 0) {
        outcome = Shown::Kind::Refused;
        outcome_from = from;
        outcome_why = std::to_string(tries) + " WRONG CODES";
        outcome_at = now;
        paused[from] = now + refused_pause;
        code.clear();
        return {410, 0, {}, std::to_string(tries) + " wrong codes: refused"};
    }
    return {403, left, {}, "Wrong code"};
}

bool Pairing::allowed(const std::string& token, const std::string& from) const {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> held(lock);
    return std::any_of(tokens.begin(), tokens.end(), [&](const Paired& p) { return same(p.token, token) && p.from == from; });
}

void Pairing::refuse(Clock::time_point now) {
    std::lock_guard<std::mutex> held(lock);
    expire(now);
    if (code.empty()) return;
    outcome = Shown::Kind::Refused;
    outcome_from = asker;
    outcome_why = "BACK ON THE MOVE";
    outcome_at = now;
    paused[asker] = now + refused_pause;
    code.clear();
}

std::string Pairing::state(const std::string& from, Clock::time_point now, unsigned& seconds) const {
    std::lock_guard<std::mutex> held(lock);
    expire(now);
    seconds = 0;
    if (!code.empty() && asker == from) {
        seconds = seconds_until(ends, now);
        return "waiting";
    }
    if (outcome_from == from) {
        if (outcome == Shown::Kind::Paired) return "paired";
        if (outcome == Shown::Kind::Refused) return outcome_why == "TIME UP" ? "expired" : "refused";
    }
    return "none";
}

Pairing::Shown Pairing::shown(Clock::time_point now) const {
    std::lock_guard<std::mutex> held(lock);
    expire(now);
    Shown out;
    if (!code.empty()) {
        out.kind = Shown::Kind::Code;
        out.code = code;
        out.from = asker;
        out.seconds = seconds_until(ends, now);
    } else if (outcome != Shown::Kind::Nothing && now - outcome_at < shown_after) {
        out.kind = outcome;
        out.from = outcome_from;
        out.why = outcome_why;
    }
    return out;
}

unsigned Pairing::paired() const {
    std::lock_guard<std::mutex> held(lock);
    std::vector<std::string> froms;
    for (const auto& p : tokens)
        if (std::find(froms.begin(), froms.end(), p.from) == froms.end()) froms.push_back(p.from);
    return unsigned(froms.size());
}

} // namespace web

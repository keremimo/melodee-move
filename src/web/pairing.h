// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Who may use Melodee's web page on the Move (docs/UI.md, "The web page"),
// as Kerem chose on 2026-11-20: a browser on the USB cable at once; any
// other (Wi-Fi) only after typing a code the Move's screen shows, until
// melodee-move stops.
namespace web {

// How a connection reached the Move.
enum class Via : uint8_t { Usb, Local, Other };

// The Move's network as the page needs it: its USB address and network, and
// the one it has on Wi-Fi (or any other), from the system's interfaces.
struct Network {
    struct Interface { std::string name, address; uint32_t ip = 0, mask = 0; };
    std::vector<Interface> interfaces; // IPv4, up
    static Network now();              // getifaddrs()
    // A connection is on the USB cable when it came in on the address of an
    // interface named usb* and from an address on that interface's own
    // network: a host on Wi-Fi sending to the USB address from elsewhere is
    // not, and one claiming a USB-side address never gets its replies.
    // Loopback (from the Move itself) is Local.
    Via via(const std::string& local, const std::string& peer) const;
    std::string usb() const;  // the first usb* address, or empty
    std::string wifi() const; // the first other address (wlan0, eth0), or empty
};

class Pairing {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto code_life = std::chrono::seconds(90);
    static constexpr auto ask_gap = std::chrono::seconds(3);   // between asks for a code, from anyone
    static constexpr auto shown_after = std::chrono::seconds(3); // PAIRED or REFUSED stays this long
    // A browser refused (Back on the Move, or five wrong codes) may not ask
    // again for this long: a device on the Wi-Fi cannot keep taking the
    // Move's screen and buttons with codes nobody wants.
    static constexpr auto refused_pause = std::chrono::minutes(5);
    static constexpr unsigned tries = 5;
    static constexpr size_t most_paired = 32;

    // `random`: n bytes of randomness (/dev/urandom by default; tests pass their own).
    explicit Pairing(std::function<void(uint8_t*, size_t)> random = {});

    // Any thread but the audio thread.
    struct Asked { int status = 200; unsigned seconds = 0; std::string error; };
    // A browser at `from` asks for a code: a new one goes on the Move's
    // screen (the one before it, if any, from the same browser, is gone).
    // Another browser's code still showing, one asked for under `ask_gap`
    // ago, or this browser refused under `refused_pause` ago, makes it wait
    // (429).
    Asked ask(const std::string& from, Clock::time_point now);
    struct Tried { int status = 200; unsigned left = 0; std::string token, error; };
    // The code typed at `from`: 200 with a token for its cookie; 403 with
    // the tries left; 410 once no code is waiting for it (never asked,
    // refused, run out, or the last try wrong).
    Tried attempt(const std::string& from, const std::string& code, Clock::time_point now);
    // Whether a cookie's token pairs the browser at `from`.
    bool allowed(const std::string& token, const std::string& from) const;
    // Back on the Move, while a code shows: refused.
    void refuse(Clock::time_point now);
    // What a browser at `from` asking about its code is told: "waiting",
    // "refused", "expired" or "none", and the seconds left while waiting.
    std::string state(const std::string& from, Clock::time_point now, unsigned& seconds) const;

    // For the Move's screen.
    struct Shown {
        enum class Kind : uint8_t { Nothing, Code, Paired, Refused } kind = Kind::Nothing;
        std::string code, from, why;
        unsigned seconds = 0;
    };
    Shown shown(Clock::time_point now) const;
    unsigned paired() const;

private:
    std::string fresh_code();
    std::string fresh_token();
    void expire(Clock::time_point now) const;
    std::function<void(uint8_t*, size_t)> random;
    mutable std::mutex lock;
    // The code showing, if any. Mutable: a look at it lets one that ran out go.
    mutable std::string code, asker;
    mutable Clock::time_point ends {};
    mutable unsigned left = 0;
    Clock::time_point last_ask {};
    // The last outcome, for the screen and for the browser that asked.
    mutable Shown::Kind outcome = Shown::Kind::Nothing;
    mutable std::string outcome_from, outcome_why;
    mutable Clock::time_point outcome_at {};
    struct Paired { std::string token, from; };
    std::vector<Paired> tokens;
    std::map<std::string, Clock::time_point> paused; // refused browsers, until when
};

} // namespace web

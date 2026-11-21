// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/display.h"
#include "web/http.h"
#include "web/pairing.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

// Melodee's web page on the Move (docs/GUIDE.md, "The web page"):
// http://move.local:8080, on its own threads, never the audio thread.
//
//   Screen  the Move's screen, live: each changed frame as a server-sent
//           event, at most 30 a second (the Move's answer to the MPC's USB
//           webcam)
//   Files   Melodee's folder (/data/UserData/Melodee: samples, projects):
//           browse, play, download, upload, rename, delete, new folders;
//           nothing outside it, links followed to check
//   Guide   docs/GUIDE.md, carried in the binary
//
// A browser on the USB cable uses it at once; any other first types the
// code the Move shows (pairing.h). Ableton's own page keeps port 80
// (MoveWebService runs on while Ableton's app is stopped); Schwung has 7700
// and 7681. This one takes 8080, or the next free port up to 8089.
namespace web {

constexpr uint16_t default_port = 8080;
constexpr unsigned port_tries = 10;

// The Move's screen as the page shows it: the newest frame, for the page's
// streams. The screen thread publishes; any number of streams wait.
class Frames {
public:
    void publish(const move::Frame& frame); // only a changed frame wakes the streams
    // Waits up to `limit` for a frame newer than `seen`: true with it in `out`.
    bool next(uint64_t& seen, move::Frame& out, std::chrono::milliseconds limit);
    void close(); // every wait ends
    uint64_t count() const { return published.load(); }

private:
    std::mutex lock;
    std::condition_variable changed;
    move::Frame frame {};
    std::atomic<uint64_t> published {0};
    bool closing = false;
};

struct PageOptions {
    std::string home;              // Melodee's folder: /data/UserData/Melodee on the Move
    uint16_t port = default_port;  // the first to try; 0 any free port (tests)
    unsigned tries = port_tries;
};

class WebPage {
public:
    explicit WebPage(PageOptions options);
    ~WebPage() { stop(); }
    bool start(std::string& error);
    void stop();
    uint16_t port() const { return server.port(); }
    Pairing& pairing() { return pair; }
    const Pairing& pairing() const { return pair; }
    Frames& frames() { return screen; }
    const Frames& frames() const { return screen; }
    // A browser changed Melodee's folder (uploaded, renamed, deleted): look
    // again. Any thread; true once per change.
    bool take_files_changed() { return files_changed.exchange(false); }

    Reply handle(const Request& request, const Body& body); // the server's threads; public for the tests

private:
    Reply status(const Request& request, Via via);
    Reply pair_state(const Request& request, Via via);
    Reply pair_ask(const Request& request, Via via);
    Reply pair_try(const Request& request, Via via);
    Reply stream_screen();
    Reply list(const Request& request);
    Reply send_file(const Request& request);
    Reply upload(const Request& request, const Body& body);
    Reply make_folder(const Request& request);
    Reply rename(const Request& request);
    Reply remove(const Request& request);
    bool place(const std::string& rel, std::string& out) const; // under `home`, or false
    void adopt(const std::string& path) const;                  // given to the Move's user when we run as root

    PageOptions options;
    Server server;
    Pairing pair;
    Frames screen;
    std::atomic<bool> files_changed {false};
    std::atomic<unsigned> uploads {0};
};

// The version this build is (module/module.json's), as the page shows it.
const char* version();

} // namespace web

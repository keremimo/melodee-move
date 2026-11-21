// SPDX-License-Identifier: AGPL-3.0-only
// Tests for Melodee's web page on the Move (src/web/): pairing and the USB
// rule, the page's answers and Melodee's folder through them, the server
// over real sockets with the screen's stream, and the layer over an app.
#include "web/http.h"
#include "web/pairing.h"
#include "web/screens.h"
#include "web/web_layer.h"
#include "web/web_page.h"
#include "web/zip_stream.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

// Counting bytes: a known run of codes and tokens.
std::function<void(uint8_t*, size_t)> counting() {
    auto next = std::make_shared<uint8_t>(0);
    return [next](uint8_t* into, size_t size) { for (size_t n = 0; n < size; ++n) into[n] = (*next)++; };
}

void test_pairing() {
    web::Pairing pairing(counting());
    auto t = Clock::now();
    const std::string phone = "192.168.1.40", other = "192.168.1.41";
    CHECK(pairing.shown(t).kind == web::Pairing::Shown::Kind::Nothing);
    CHECK(pairing.attempt(phone, "000000", t).status == 410); // nothing asked yet

    auto asked = pairing.ask(phone, t);
    CHECK(asked.status == 200 && asked.seconds == 90);
    auto shown = pairing.shown(t);
    CHECK(shown.kind == web::Pairing::Shown::Kind::Code && shown.code.size() == 6 && shown.from == phone && shown.seconds == 90);
    CHECK(web::spaced_code("482913") == "482 913");
    unsigned seconds = 0;
    CHECK(pairing.state(phone, t + std::chrono::seconds(10), seconds) == "waiting" && seconds == 80);
    CHECK(pairing.ask(other, t + std::chrono::seconds(5)).status == 429); // another browser waits its turn
    CHECK(pairing.ask(phone, t + std::chrono::seconds(1)).status == 429); // too soon, even for the same one

    std::string wrong = shown.code == "000000" ? "111111" : "000000";
    for (unsigned n = 4; n >= 1; --n) {
        auto tried = pairing.attempt(phone, wrong, t);
        CHECK(tried.status == 403 && tried.left == n);
    }
    CHECK(pairing.attempt(other, shown.code, t).status == 410); // the code is the asker's alone
    CHECK(pairing.attempt(phone, wrong, t).status == 410);      // the fifth: refused
    shown = pairing.shown(t);
    CHECK(shown.kind == web::Pairing::Shown::Kind::Refused && shown.why == "5 WRONG CODES");
    CHECK(pairing.state(phone, t, seconds) == "refused");
    CHECK(pairing.shown(t + std::chrono::seconds(4)).kind == web::Pairing::Shown::Kind::Nothing);

    // Refused: that browser waits five minutes before it may ask again.
    t += std::chrono::seconds(10);
    CHECK(pairing.ask(phone, t).status == 429);
    t += std::chrono::minutes(5);

    // Asked again: the right code pairs, the token for that browser only.
    CHECK(pairing.ask(phone, t).status == 200);
    auto code = pairing.shown(t).code;
    auto tried = pairing.attempt(phone, code, t);
    CHECK(tried.status == 200 && tried.token.size() == 32);
    CHECK(pairing.allowed(tried.token, phone));
    CHECK(!pairing.allowed(tried.token, other));
    CHECK(!pairing.allowed("", phone));
    CHECK(!pairing.allowed(std::string(32, '0'), phone));
    CHECK(pairing.shown(t).kind == web::Pairing::Shown::Kind::Paired);
    CHECK(pairing.paired() == 1);
    CHECK(pairing.attempt(phone, code, t).status == 410); // used

    // Back on the Move refuses; a code nobody types runs out.
    t += std::chrono::seconds(10);
    CHECK(pairing.ask(other, t).status == 200);
    pairing.refuse(t);
    CHECK(pairing.shown(t).why == "BACK ON THE MOVE" && pairing.state(other, t, seconds) == "refused");
    t += std::chrono::seconds(10);
    CHECK(pairing.ask(other, t).status == 429); // refused on the Move: not again for a while
    CHECK(pairing.ask("192.168.1.42", t).status == 200);
    t += std::chrono::seconds(100); // that one runs out unanswered: no pause for it
    CHECK(pairing.ask("192.168.1.42", t).status == 200);
    pairing.attempt("192.168.1.42", pairing.shown(t).code, t);
    t += std::chrono::minutes(5);
    CHECK(pairing.ask(other, t).status == 200);
    CHECK(pairing.state(other, t + std::chrono::seconds(91), seconds) == "expired");
    CHECK(pairing.shown(t + std::chrono::seconds(91)).why == "TIME UP");

    // Codes are six digits, from the random bytes.
    web::Pairing fresh;
    auto now = Clock::now();
    CHECK(fresh.ask("10.0.0.2", now).status == 200);
    auto real = fresh.shown(now).code;
    CHECK(real.size() == 6 && real.find_first_not_of("0123456789") == std::string::npos);
}

void test_usb_rule() {
    web::Network network;
    network.interfaces = {{"lo", "127.0.0.1", 0x7f000001, 0xff000000}, {"usb0", "172.16.42.1", 0xac102a01, 0xffffff00},
                          {"wlan0", "192.168.1.23", 0xc0a80117, 0xffffff00}};
    CHECK(network.via("172.16.42.1", "172.16.42.2") == web::Via::Usb);
    CHECK(network.via("172.16.42.1", "192.168.1.40") == web::Via::Other); // sent to the USB address from Wi-Fi
    CHECK(network.via("192.168.1.23", "192.168.1.40") == web::Via::Other);
    CHECK(network.via("192.168.1.23", "172.16.42.2") == web::Via::Other); // the USB side, to the Wi-Fi address
    CHECK(network.via("127.0.0.1", "127.0.0.1") == web::Via::Local);
    CHECK(network.via("", "1.2.3.4") == web::Via::Other);
    CHECK(network.usb() == "172.16.42.1" && network.wifi() == "192.168.1.23");
}

web::Request request(const std::string& method, const std::string& path, std::map<std::string, std::string> query = {},
                     const std::string& peer = "127.0.0.1") {
    web::Request r;
    r.method = method;
    r.path = path;
    r.query = std::move(query);
    r.local = peer == "127.0.0.1" ? "127.0.0.1" : "10.9.8.7";
    r.peer = peer;
    r.headers["host"] = "move.local:8080";
    return r;
}

web::Body body_of(const std::string& data) {
    auto at = std::make_shared<size_t>(0);
    return [data, at](char* into, size_t size) -> long {
        size_t n = std::min(size, data.size() - *at);
        std::memcpy(into, data.data() + *at, n);
        *at += n;
        return long(n);
    };
}
const web::Body none = [](char*, size_t) -> long { return 0; };

void test_page(const fs::path& scratch) {
    const fs::path home = scratch / "Melodee";
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    web::WebPage page({home.string(), 0, 1});

    // The page itself is open to any browser: it holds nothing of the Move's.
    auto index = page.handle(request("GET", "/", {}, "10.0.0.5"), none);
    CHECK(index.status == 200 && index.type.find("text/html") == 0 && index.body.find("Melodee") != std::string::npos);
    CHECK(index.body.find("The kit track") != std::string::npos); // the guide is in it
    CHECK(page.handle(request("GET", "/files"), none).body == index.body);
    CHECK(page.handle(request("GET", "/app.js"), none).type.find("javascript") != std::string::npos);
    CHECK(page.handle(request("GET", "/font/rubik.ttf"), none).body.size() > 100000);
    CHECK(page.handle(request("GET", "/nothing"), none).status == 404);

    // From Wi-Fi: nothing of the Move's before pairing.
    const std::string phone = "10.0.0.5";
    CHECK(page.handle(request("GET", "/api/status", {}, phone), none).status == 401);
    CHECK(page.handle(request("GET", "/api/list", {{"path", ""}}, phone), none).status == 401);
    CHECK(page.handle(request("GET", "/api/screen", {}, phone), none).status == 401);
    CHECK(page.handle(request("POST", "/api/upload", {{"path", ""}, {"name", "x.wav"}}, phone), none).status == 401);
    CHECK(page.handle(request("GET", "/api/pair", {}, phone), none).body.find("\"none\"") != std::string::npos);
    CHECK(page.handle(request("POST", "/api/pair/ask", {}, phone), none).status == 200);
    CHECK(page.handle(request("GET", "/api/pair", {}, phone), none).body.find("\"waiting\"") != std::string::npos);
    auto wrong = page.handle(request("POST", "/api/pair", {{"code", "abc"}}, phone), none);
    CHECK(wrong.status == 403 && wrong.body.find("\"left\":4") != std::string::npos);
    auto code = page.pairing().shown(Clock::now()).code;
    auto paired = page.handle(request("POST", "/api/pair", {{"code", code}}, phone), none);
    CHECK(paired.status == 200 && paired.headers.size() == 1 && paired.headers[0].first == "Set-Cookie");
    std::string cookie = paired.headers[0].second.substr(0, paired.headers[0].second.find(';'));
    CHECK(paired.headers[0].second.find("HttpOnly") != std::string::npos && paired.headers[0].second.find("SameSite=Strict") != std::string::npos);
    auto with = request("GET", "/api/status", {}, phone);
    with.headers["cookie"] = "other=1; " + cookie;
    auto status = page.handle(with, none);
    CHECK(status.status == 200 && status.body.find("\"via\":\"wifi\"") != std::string::npos);
    auto stolen = request("GET", "/api/status", {}, "10.0.0.6");
    stolen.headers["cookie"] = cookie;
    CHECK(page.handle(stolen, none).status == 401); // the cookie on another machine is nothing

    // From the Move itself (and the USB cable, by the same door): Melodee's folder.
    auto local = page.handle(request("GET", "/api/status"), none);
    CHECK(local.status == 200 && local.body.find("\"via\":\"local\"") != std::string::npos);
    auto empty = page.handle(request("GET", "/api/list", {{"path", ""}}), none);
    CHECK(empty.status == 200 && empty.body.find("\"entries\":[]") != std::string::npos);
    CHECK(!fs::exists(home)); // nothing made until something is put there

    web::Request up = request("POST", "/api/upload", {{"path", ""}, {"name", "Kick.wav"}});
    up.length = 4;
    CHECK(page.handle(up, body_of("RIFF")).status == 200);
    CHECK(fs::exists(home / "Kick.wav") && fs::file_size(home / "Kick.wav") == 4);
    CHECK(page.take_files_changed() && !page.take_files_changed());
    auto again = page.handle(up, body_of("RIFF"));
    CHECK(again.status == 200 && again.body.find("Kick 2.wav") != std::string::npos);
    web::Request short_one = up;
    short_one.length = 10;
    CHECK(page.handle(short_one, body_of("RIFF")).status == 400); // the body ends early: refused, nothing left
    for (const auto& entry : fs::directory_iterator(home)) CHECK(entry.path().filename().string()[0] != '.');

    CHECK(page.handle(request("POST", "/api/mkdir", {{"path", ""}, {"name", "Samples"}}), none).status == 200);
    CHECK(page.handle(request("POST", "/api/mkdir", {{"path", ""}, {"name", "Samples"}}), none).status == 409);
    up.query = {{"path", "Samples"}, {"name", "Snare.wav"}};
    CHECK(page.handle(up, body_of("RIFF")).status == 200 && fs::exists(home / "Samples/Snare.wav"));
    auto listed = page.handle(request("GET", "/api/list", {{"path", ""}}), none);
    CHECK(listed.body.find("\"Samples\",\"dir\":true") != std::string::npos);
    CHECK(listed.body.find("Samples") < listed.body.find("Kick.wav")); // folders first

    CHECK(page.handle(request("POST", "/api/rename", {{"path", "Kick 2.wav"}, {"to", "Hat.wav"}}), none).status == 200);
    CHECK(fs::exists(home / "Hat.wav") && !fs::exists(home / "Kick 2.wav"));
    CHECK(page.handle(request("POST", "/api/rename", {{"path", "Hat.wav"}, {"to", "Kick.wav"}}), none).status == 409);
    CHECK(page.handle(request("POST", "/api/rename", {{"path", "Hat.wav"}, {"to", "../Hat.wav"}}), none).status == 400);
    CHECK(page.handle(request("POST", "/api/rename", {{"path", ""}, {"to", "Other"}}), none).status == 403);

    auto file = page.handle(request("GET", "/api/file", {{"path", "Samples/Snare.wav"}, {"save", "1"}}), none);
    CHECK(file.status == 200 && file.type == "audio/wav" && file.file == (home / "Samples/Snare.wav").string() && file.download == "Snare.wav");

    // A folder saved as a zip (an Ableton Live Set's project): its files under its name, stored, read back whole.
    fs::create_directories(home / "Samples/Kits/.cache");
    std::ofstream(home / "Samples/Kits/Hat.wav") << "RIFF hat";
    std::ofstream(home / "Samples/Kits/.cache/x") << "hidden";
    auto zipped = page.handle(request("GET", "/api/file", {{"path", "Samples"}, {"save", "1"}}), none);
    CHECK(zipped.status == 200 && zipped.type == "application/zip" && zipped.download == "Samples.zip" && zipped.stream);
    std::string bytes;
    if (zipped.stream) zipped.stream([&](const std::string& part) { bytes += part; return true; });
    std::ofstream("build/test-web.zip", std::ios::binary) << bytes; // for a look with other unzips
    CHECK(!zipped.headers.empty() && zipped.headers[0].first == "Content-Length" && zipped.headers[0].second == std::to_string(bytes.size()));
    {
        juce::MemoryInputStream in(bytes.data(), bytes.size(), false);
        juce::ZipFile zip(in);
        CHECK(zip.getNumEntries() == 2); // Snare.wav and Kits/Hat.wav; nothing hidden
        auto read = [&](const char* name) {
            int index = zip.getIndexOfFileName(name);
            if (index < 0) return std::string("missing");
            std::unique_ptr<juce::InputStream> entry(zip.createStreamForEntry(index));
            return entry ? entry->readEntireStreamAsString().toStdString() : std::string("unreadable");
        };
        CHECK(read("Samples/Kits/Hat.wav") == "RIFF hat" && read("Samples/Snare.wav") == "RIFF");
    }
    CHECK(web::crc32_of("123456789", 9) == 0xcbf43926u); // the check value of CRC-32
    CHECK(page.handle(request("GET", "/api/file", {{"path", "Samples"}}), none).status == 404); // a folder plays nothing

    // Nothing outside Melodee's folder, by name or by link.
    CHECK(page.handle(request("GET", "/api/list", {{"path", ".."}}), none).status == 404);
    CHECK(page.handle(request("GET", "/api/file", {{"path", "Samples/../../x"}}), none).status == 404);
    CHECK(page.handle(request("GET", "/api/list", {{"path", ".hidden"}}), none).status == 404);
    std::ofstream(scratch / "secret.txt") << "no";
    fs::create_symlink(scratch, home / "out");
    CHECK(page.handle(request("GET", "/api/list", {{"path", "out"}}), none).status == 404);
    CHECK(page.handle(request("GET", "/api/file", {{"path", "out/secret.txt"}}), none).status == 404);
    CHECK(page.handle(request("POST", "/api/delete", {{"path", "out/secret.txt"}}), none).status == 403);
    up.query = {{"path", ""}, {"name", "a/b.wav"}};
    CHECK(page.handle(up, body_of("RIFF")).status == 400);
    up.query = {{"path", ""}, {"name", ".profile"}};
    CHECK(page.handle(up, body_of("RIFF")).status == 400);

    CHECK(page.handle(request("POST", "/api/delete", {{"path", "Samples"}}), none).status == 200 && !fs::exists(home / "Samples"));
    CHECK(page.handle(request("POST", "/api/delete", {{"path", ""}}), none).status == 403 && fs::exists(home));
    CHECK(fs::exists(scratch / "secret.txt"));
}

// A request over a real socket to 127.0.0.1:port; the whole reply.
std::string fetch(uint16_t port, const std::string& text, size_t enough = 0) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) { ::close(fd); return {}; }
    timeval limit {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
    ::send(fd, text.data(), text.size(), MSG_NOSIGNAL);
    std::string reply;
    char chunk[4096];
    for (ssize_t got; (got = ::recv(fd, chunk, sizeof(chunk), 0)) > 0;) {
        reply.append(chunk, size_t(got));
        if (enough && reply.size() >= enough) break;
    }
    ::close(fd);
    return reply;
}

void test_server(const fs::path& scratch) {
    fs::remove_all(scratch);
    web::WebPage page({(scratch / "Melodee").string(), 0, 1});
    std::string error;
    CHECK(page.start(error));
    const uint16_t port = page.port();
    CHECK(port != 0);
    auto index = fetch(port, "GET / HTTP/1.1\r\nHost: move.local:8080\r\n\r\n");
    CHECK(index.compare(0, 15, "HTTP/1.1 200 OK") == 0 && index.find("X-Frame-Options: DENY") != std::string::npos);
    CHECK(fetch(port, "GET / HTTP/1.1\r\nHost: evil.example\r\n\r\n").compare(0, 12, "HTTP/1.1 421") == 0); // DNS rebinding
    CHECK(fetch(port, "POST /api/mkdir?path=&name=X HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n").compare(0, 12, "HTTP/1.1 403") == 0);
    CHECK(fetch(port, "OPTIONS / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").compare(0, 12, "HTTP/1.1 405") == 0);
    auto made = fetch(port, "POST /api/mkdir?path=&name=Samples HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Melodee: 1\r\nContent-Length: 0\r\n\r\n");
    CHECK(made.compare(0, 15, "HTTP/1.1 200 OK") == 0 && fs::is_directory(scratch / "Melodee/Samples"));
    auto sent = fetch(port, "POST /api/upload?path=Samples&name=Tom.wav HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Melodee: 1\r\nContent-Length: 8\r\n\r\nRIFFWAVE");
    CHECK(sent.compare(0, 15, "HTTP/1.1 200 OK") == 0 && fs::file_size(scratch / "Melodee/Samples/Tom.wav") == 8);
    auto got = fetch(port, "GET /api/file?path=Samples/Tom.wav HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    CHECK(got.size() > 8 && got.substr(got.size() - 8) == "RIFFWAVE" && got.find("Content-Type: audio/wav") != std::string::npos);

    // The screen: a frame published comes out of the stream as base64.
    move::Frame frame {};
    frame[0] = 0x81;
    frame[1023] = 0xff;
    page.frames().publish(frame);
    auto stream = fetch(port, "GET /api/screen HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", 400);
    CHECK(stream.find("Content-Type: text/event-stream") != std::string::npos);
    size_t data = stream.find("data: ");
    CHECK(data != std::string::npos && stream.compare(data + 6, 4, "gQAA") == 0); // 0x81 0x00 0x00...
    CHECK(stream.find("\n\n", data) != std::string::npos);

    // Stop ends an open stream at once.
    std::thread open([&] { fetch(port, "GET /api/screen HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto before = Clock::now();
    page.stop();
    open.join();
    CHECK(Clock::now() - before < std::chrono::seconds(3));
}

// An app that remembers what reached it.
class Recorder : public move::App {
public:
    void render(const move::AudioBlock&, move::AudioBlock&) override {}
    void surface(const move::SurfaceEvent& event) override { seen.push_back(event.input); }
    void draw(juce::Graphics& g) override { g.fillRect(0, 0, 4, 4); ++draws; }
    std::string status() const override { return "app=yes"; }
    std::string command(const std::string& line) override { commands.push_back(line); return {}; }
    std::vector<move::Input> seen;
    std::vector<std::string> commands;
    int draws = 0;
};

move::SurfaceEvent button(move::Button which, bool pressed) {
    move::SurfaceEvent event;
    event.input.kind = move::Input::Kind::Button;
    event.input.index = uint8_t(which);
    event.input.pressed = pressed;
    return event;
}

void test_layer(const fs::path& scratch) {
    fs::remove_all(scratch);
    Recorder app;
    web::WebLayer layer(app, {(scratch / "Melodee").string(), 0, 1});
    move::Session session;
    layer.attach(session);
    juce::Image image(juce::Image::ARGB, 128, 64, true);
    {
        juce::Graphics g(image);
        layer.draw(g);
    }
    CHECK(app.draws == 1 && layer.page().frames().count() == 1);
    CHECK(image.getPixelAt(1, 1).getBrightness() > 0.5f); // the app's drawing reached the screen
    CHECK(layer.status().find("app=yes web=") == 0);

    // Shift + Capture: the address sheet; its buttons are its own until Back.
    session.shift = true;
    layer.surface(button(move::Button::Capture, true));
    session.shift = false;
    layer.surface(button(move::Button::Capture, false));
    CHECK(layer.showing_where() && app.seen.empty());
    layer.surface(button(move::Button::Loop, true));
    layer.surface(button(move::Button::Loop, false));
    CHECK(app.seen.size() == 1 && !app.seen[0].pressed); // the release only
    move::SurfaceEvent pad;
    pad.input.kind = move::Input::Kind::Pad;
    pad.input.pressed = true;
    layer.surface(pad);
    CHECK(app.seen.size() == 2); // pads still play
    layer.surface(button(move::Button::Back, true));
    CHECK(!layer.showing_where());
    layer.surface(button(move::Button::Back, false));
    layer.surface(button(move::Button::Loop, true));
    CHECK(app.seen.size() == 4); // Back's release, then Loop: the app's again

    // A Wi-Fi browser's code: on the screen and the page's mirror; Back refuses.
    auto now = Clock::now();
    CHECK(layer.page().pairing().ask("10.0.0.5", now).status == 200);
    {
        juce::Graphics g(image);
        layer.draw(g);
    }
    CHECK(layer.page().frames().count() == 2);
    CHECK(image.getPixelAt(1, 1).getBrightness() > 0.5f && image.getPixelAt(23, 1).getBrightness() > 0.5f); // the rail
    size_t before = app.seen.size();
    layer.surface(button(move::Button::Back, true));
    CHECK(app.seen.size() == before);
    unsigned seconds = 0;
    CHECK(layer.page().pairing().state("10.0.0.5", Clock::now(), seconds) == "refused");

    // A file put into Melodee's folder from the page: the app's browser and projects look again, once.
    web::Request up = request("POST", "/api/upload", {{"path", ""}, {"name", "Snare.wav"}});
    up.length = 4;
    CHECK(layer.page().handle(up, body_of("RIFF")).status == 200);
    layer.service();
    CHECK(app.commands.size() == 1 && app.commands[0] == "melodee-files-changed");
    layer.service();
    CHECK(app.commands.size() == 1);
}

} // namespace

int main() {
    auto scratch = fs::temp_directory_path() / ("melodee-web-tests-" + std::to_string(::getpid()));
    test_pairing();
    test_usb_rule();
    test_page(scratch / "page");
    test_server(scratch / "server");
    test_layer(scratch / "layer");
    fs::remove_all(scratch);
    std::cout << "web-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}

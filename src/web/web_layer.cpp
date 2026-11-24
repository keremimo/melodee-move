// SPDX-License-Identifier: AGPL-3.0-only
#include "web/web_layer.h"
#include "move/display.h"
#include "move/lights.h"
#include <cstdio>

namespace web {
namespace {
constexpr auto look_interval = std::chrono::seconds(2); // the Move's addresses, looked at again
}

WebLayer::WebLayer(move::App& app, PageOptions options)
    : inner(app), web(std::move(options)), image(juce::Image::ARGB, move::screen_width, move::screen_height, true) {
    std::string error;
    serving = web.start(error);
    if (!serving) std::fprintf(stderr, "melodee-move: web: no page (%s)\n", error.c_str());
}

WebLayer::~WebLayer() { web.stop(); }

void WebLayer::attach(const move::Session& session) {
    App::attach(session);
    inner.attach(session);
}

bool WebLayer::sheet_up(Clock::time_point now) const {
    return where_shown.load() || web.pairing().shown(now).kind == Pairing::Shown::Kind::Code;
}

void WebLayer::surface(const move::SurfaceEvent& event) {
    using move::Button;
    using Kind = move::Input::Kind;
    const auto& input = event.input;
    const auto now = Clock::now();
    const bool button = input.kind == Kind::Button;
    if (button && input.index == uint8_t(Button::Capture)) {
        if (input.pressed && session().shift) {
            where_shown = !where_shown.load();
            capture_taken = true;
            return;
        }
        if (!input.pressed && capture_taken.exchange(false)) return;
    }
    if (sheet_up(now)) {
        // The sheet's: a button's press and the jogwheel's turn. Releases still reach the app, so nothing stays held.
        if (button && input.pressed) {
            if (input.index == uint8_t(Button::Back)) {
                if (web.pairing().shown(now).kind == Pairing::Shown::Kind::Code) web.pairing().refuse(now);
                else where_shown = false;
            }
            return;
        }
        if (input.kind == Kind::MainKnob) return;
    }
    inner.surface(event);
}

void WebLayer::lights(move::Lights& lights, Clock::time_point now) {
    inner.lights(lights, now);
    if (sheet_up(now)) lights.button(move::Button::Back, move::white_bright);
}

void WebLayer::draw(juce::Graphics& g) {
    const auto now = Clock::now();
    {
        juce::Graphics own(image);
        own.fillAll(juce::Colours::black);
        own.setColour(juce::Colours::white);
        inner.draw(own); // always, so the app's screen keeps its own time
        own.setColour(juce::Colours::white);
        auto shown = web.pairing().shown(now);
        using Kind = Pairing::Shown::Kind;
        if (shown.kind == Kind::Code) draw_code(own, shown.code, shown.seconds, shown.from);
        else if (shown.kind == Kind::Paired) draw_paired(own, shown.from);
        else if (shown.kind == Kind::Refused) draw_refused(own, shown.why);
        else if (where_shown.load()) {
            std::lock_guard<std::mutex> held(where_lock);
            draw_where(own, where);
        }
    }
    g.drawImageAt(image, 0, 0);
    web.frames().publish(move::pack(image));
}

bool WebLayer::draw_power(juce::Graphics& g) {
    bool drawn = false;
    {
        juce::Graphics own(image);
        own.fillAll(juce::Colours::black);
        own.setColour(juce::Colours::white);
        drawn = inner.draw_power(own);
    }
    if (!drawn) return false;
    g.drawImageAt(image, 0, 0);
    web.frames().publish(move::pack(image));
    return true;
}

void WebLayer::service() {
    inner.service();
    if (web.take_files_changed()) inner.command("melodee-files-changed"); // Load and the projects look again
    if (!serving) return;
    const auto now = Clock::now();
    if (now < next_look) return;
    next_look = now + look_interval;
    auto network = Network::now();
    Where looked {network.usb(), network.wifi(), web.port(), web.pairing().paired()};
    {
        std::lock_guard<std::mutex> held(where_lock);
        where = looked;
    }
    // A line in the log when the addresses change: where to open the page.
    std::string now_said = looked.usb + "|" + looked.wifi;
    if (now_said != said) {
        said = now_said;
        std::fprintf(stderr, "melodee-move: web page at http://move.local:%u (USB %s: open; Wi-Fi %s: asks a code)\n",
                     unsigned(looked.port), looked.usb.empty() ? "none" : looked.usb.c_str(), looked.wifi.empty() ? "none" : looked.wifi.c_str());
    }
}

std::string WebLayer::status() const {
    std::string mine = serving ? "web=" + std::to_string(web.port()) + " web_paired=" + std::to_string(web.pairing().paired())
        + " web_frames=" + std::to_string(web.frames().count()) : std::string("web=off");
    std::string theirs = inner.status();
    return theirs.empty() ? mine : theirs + " " + mine;
}

} // namespace web

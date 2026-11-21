// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/app.h"
#include "web/screens.h"
#include "web/web_page.h"
#include <atomic>
#include <mutex>
#include <string>

// Melodee's web page around an app (web_page.h): the app runs as it would
// alone, every hook passed on, and this layer adds what the page needs on
// the Move:
//
// - each frame the app draws goes to the page's screen as well;
// - the page's screens over the app's (screens.h): Shift + Capture, where
//   the page is; a Wi-Fi browser's code while it waits (Back refuses);
//   PAIRED or REFUSED for a moment after;
// - while the address or a code shows, the buttons and the jogwheel are
//   the sheet's (Back closes or refuses); pads, steps and knobs still play,
//   and releases always reach the app.
//
// Pads still go straight to the app's audio_event() on the audio thread;
// nothing here is on that path. The page's server has threads of its own.
namespace web {

class WebLayer : public move::App {
public:
    WebLayer(move::App& app, PageOptions options);
    ~WebLayer() override;

    void attach(const move::Session& session) override;
    void render(const move::AudioBlock& input, move::AudioBlock& output) override { inner.render(input, output); }
    void audio_opened(const move::AudioFormat& format) override { inner.audio_opened(format); }
    void audio_event(const move::SurfaceEvent& event) override { inner.audio_event(event); }
    void surface(const move::SurfaceEvent& event) override;                 // MIDI thread
    void midi(const move::ExternalMidi& message) override { inner.midi(message); }
    unsigned midi_out(move::ExternalMidi* messages, unsigned room) override { return inner.midi_out(messages, room); } // audio thread
    void lights(move::Lights& lights, Clock::time_point now) override;      // MIDI thread
    void draw(juce::Graphics& g) override;                                  // screen thread
    void service() override;                                                // service thread
    void stopping() override { inner.stopping(); }                          // service thread, the run ending
    bool finished() const override { return inner.finished(); }
    std::string status() const override;

    WebPage& page() { return web; }
    bool showing_where() const { return where_shown.load(); }

private:
    bool sheet_up(Clock::time_point now) const; // the address or a code: the buttons are the sheet's
    move::App& inner;
    WebPage web;
    bool serving = false;
    std::atomic<bool> where_shown {false};
    std::atomic<bool> capture_taken {false}; // Shift + Capture's press was ours: so is its release
    mutable std::mutex where_lock;           // the addresses, from the service thread for the screen
    Where where;
    Clock::time_point next_look {};
    std::string said; // the addresses last logged
    juce::Image image;
};

} // namespace web

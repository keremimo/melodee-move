// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/controls.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <string>

// The Move's power button on Ableton's firmware, as Ableton's app answers
// it (captured on a Move 2026-11-23, Schwung's spi_midi_log and
// dbus-monitor). A short press sets "shut down?" in the XMOS's power state,
// which it reports on USB-MIDI cable 14 as a control change on channel 3:
// the state's flags as the controller, the battery's charge (percent) as
// the value, again at every change of either (the same report comes as
// Ableton SysEx 3A on cable 0). The app answers with Ableton SysEx 39 on
// cable 0: 02 no, and the XMOS drops the flag; 06 yes, then it asks
// com.ableton.system (SystemDBusService, root) to shut down, and halt's
// /etc/init.d/xmos-power-off cuts the power. Held for ten seconds the
// button cuts it in the XMOS alone.
namespace move {

namespace power {

constexpr uint8_t report_cable = 14;
constexpr uint8_t report_status = 0xb2;
constexpr uint8_t asking_flag = 0x08;
using Answer = std::array<uint8_t, 9>;
constexpr Answer answer_no {0xf0, 0x00, 0x21, 0x1d, 0x01, 0x01, 0x39, 0x02, 0xf7};
constexpr Answer answer_yes {0xf0, 0x00, 0x21, 0x1d, 0x01, 0x01, 0x39, 0x06, 0xf7};

// Asks com.ableton.system to shut the Move down, as Ableton's app does
// (dbus-send on the system bus; its policy lets root and ableton ask).
// Blocks until it answers. False, with why, when it did not take it.
bool shut_down(std::string& why);

} // namespace power

enum class Power : uint8_t { On, Asking, ShuttingDown };

// The question the power button opens, kept by the runtime on its MIDI
// thread. While it is open the wheel is the question's: its push says yes,
// Back says no (Shift or not), its turn and touch go nowhere; the rest of
// the surface stays the app's.
class PowerQuestion {
public:
    using Clock = std::chrono::steady_clock;
    enum class Answer : uint8_t { None, Yes, No };

    // A report from the XMOS. True when the question opened or closed.
    bool report(uint8_t state, uint8_t charge, Clock::time_point now);
    // A control from the surface. True when it is the question's and the
    // app should not see it (the release of a press it took, too); `answer`
    // is what it answered.
    bool take(const Input& input, Answer& answer, Clock::time_point now);

    Power state() const { return asked; }
    int charge() const { return battery; }

private:
    Power asked = Power::On;
    int battery = -1;
    // The XMOS reports the flag gone a few exchanges after a no: until then
    // (or a second) a report still carrying it is that same press.
    bool answered = false;
    Clock::time_point answered_at {};
    bool push_held = false, back_held = false; // pressed while asking: their releases are the question's
};

} // namespace move

// SPDX-License-Identifier: AGPL-3.0-only
// melodee-move: Melodee (or the spike) on the Move runtime, or one of its
// offline modes.
#include "app/melodee_app.h"
#include "app/previews.h"
#include "move/runtime.h"
#include "move/tools.h"
#include "spike/spike_app.h"
#include "ui/mockups.h"
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr <<
        "usage: melodee-move [--app melodee|spike] [--samples DIR] [--previews DIR]\n"
        "                    [--backend auto|spi|jack|simulated] [--seconds S] [--name CLIENT]\n"
        "                    [--loopback FRAMES] [--script 'TRANSFER:STATUS,DATA1,DATA2;...']\n"
        "         run on the Move: Melodee (its sample library in --samples, by default the Move's own), or the\n"
        "         spike (--previews: Melodee's pages crushed). spi is the Move's own device on Ableton's firmware,\n"
        "         jack is jackd -d move on Armbian, simulated a stand-in for the device (--loopback, --script)\n"
        "       melodee-move --self-test FILE.wav       render offline, fail if silent\n"
        "       melodee-move --preview DIR              the screen as PNGs\n"
        "       melodee-move --crush PNG|DIR OUT_DIR    320 x 240 pictures on 128 x 64\n";
}

move::Backend backend_named(const std::string& name) {
    if (name == "auto") return move::Backend::Auto;
    if (name == "spi") return move::Backend::Spi;
    if (name == "jack") return move::Backend::Jack;
    if (name == "simulated") return move::Backend::SpiSimulated;
    throw std::runtime_error("unknown backend " + name + "; use auto, spi, jack or simulated");
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string previews, samples, which = "melodee";
        move::RunOptions options;
        for (int n = 1; n < argc; ++n) {
            std::string argument = argv[n];
            bool has_value = n + 1 < argc;
            if (argument == "--self-test" && has_value) { SpikeApp app; return move::self_test(app, argv[n + 1]); }
            if (argument == "--preview" && has_value) { SpikeApp app; return move::write_previews(app, argv[n + 1]); }
            if (argument == "--crush" && n + 2 < argc) return move::crush_pictures(argv[n + 1], argv[n + 2]);
            if (argument == "--ui-mockups" && has_value) return ui::write_mockups(argv[n + 1]);
            if (argument == "--test-loop" && has_value) return move::write_test_loop(argv[n + 1]) ? 0 : 1;
            if (argument == "--melodee-previews" && has_value) return app::write_previews(argv[n + 1]);
            if (argument == "--previews" && has_value) previews = argv[++n];
            else if (argument == "--samples" && has_value) samples = argv[++n];
            else if (argument == "--app" && has_value) which = argv[++n];
            else if (argument == "--seconds" && has_value) options.seconds = std::stod(argv[++n]);
            else if (argument == "--name" && has_value) options.client_name = argv[++n];
            else if (argument == "--backend" && has_value) options.backend = backend_named(argv[++n]);
            else if (argument == "--loopback" && has_value) options.simulated_loopback_frames = unsigned(std::stoul(argv[++n]));
            else if (argument == "--script" && has_value) options.simulated_script = argv[++n];
            else { usage(); return 2; }
        }
        if (which == "spike") {
            SpikeApp app(previews);
            return move::run_app(app, options);
        }
        if (which != "melodee") { usage(); return 2; }
        auto app = std::make_unique<app::MelodeeApp>(samples);
        return move::run_app(*app, options);
    } catch (const std::exception& error) {
        std::cerr << "melodee-move: " << error.what() << '\n';
        return 1;
    }
}

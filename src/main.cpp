// SPDX-License-Identifier: AGPL-3.0-only
// melodee-move: the spike on the Move runtime, or one of its offline modes.
#include "move/runtime.h"
#include "move/tools.h"
#include "spike/spike_app.h"
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr <<
        "usage: melodee-move [--backend auto|spi|jack|simulated] [--previews DIR] [--seconds S] [--name CLIENT]\n"
        "                    [--loopback FRAMES] [--script 'TRANSFER:STATUS,DATA1,DATA2;...']\n"
        "         run on the Move: spi is the Move's own device on Ableton's firmware, jack is jackd -d move on\n"
        "         Armbian, simulated is a stand-in for the device (--loopback and --script set it up)\n"
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
        std::string previews;
        move::RunOptions options;
        for (int n = 1; n < argc; ++n) {
            std::string argument = argv[n];
            bool has_value = n + 1 < argc;
            if (argument == "--self-test" && has_value) { SpikeApp app; return move::self_test(app, argv[n + 1]); }
            if (argument == "--preview" && has_value) { SpikeApp app; return move::write_previews(app, argv[n + 1]); }
            if (argument == "--crush" && n + 2 < argc) return move::crush_pictures(argv[n + 1], argv[n + 2]);
            if (argument == "--previews" && has_value) previews = argv[++n];
            else if (argument == "--seconds" && has_value) options.seconds = std::stod(argv[++n]);
            else if (argument == "--name" && has_value) options.client_name = argv[++n];
            else if (argument == "--backend" && has_value) options.backend = backend_named(argv[++n]);
            else if (argument == "--loopback" && has_value) options.simulated_loopback_frames = unsigned(std::stoul(argv[++n]));
            else if (argument == "--script" && has_value) options.simulated_script = argv[++n];
            else { usage(); return 2; }
        }
        SpikeApp app(previews);
        return move::run_app(app, options);
    } catch (const std::exception& error) {
        std::cerr << "melodee-move: " << error.what() << '\n';
        return 1;
    }
}

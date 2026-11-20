// SPDX-License-Identifier: AGPL-3.0-only
// melodee-move: the spike on the Move runtime, or one of its offline modes.
#include "move/runtime.h"
#include "move/tools.h"
#include "spike/spike_app.h"
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

namespace {

void usage() {
    std::cerr <<
        "usage: melodee-move [--previews DIR] [--seconds S] [--name CLIENT]   run on the Move (JACK)\n"
        "       melodee-move --self-test FILE.wav                            render offline, fail if silent\n"
        "       melodee-move --preview DIR                                   the screen as PNGs\n"
        "       melodee-move --crush PNG|DIR OUT_DIR                         320 x 240 pictures on 128 x 64\n";
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
            else { usage(); return 2; }
        }
        SpikeApp app(previews);
        return move::run_app(app, options);
    } catch (const std::exception& error) {
        std::cerr << "melodee-move: " << error.what() << '\n';
        return 1;
    }
}

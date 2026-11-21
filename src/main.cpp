// SPDX-License-Identifier: AGPL-3.0-only
// melodee-move: Melodee (or the spike) on the Move runtime, or one of its
// offline modes.
#include "app/melodee_app.h"
#include "app/module_probe.h"
#include "app/previews.h"
#include "move/runtime.h"
#include "move/remote.h"
#include "move/tools.h"
#include "spike/spike_app.h"
#include "ui/mockups.h"
#include "web/web_layer.h"
#include <csignal>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr <<
        "usage: melodee-move [--app melodee|spike] [--samples DIR] [--home DIR] [--modules DIR] [--previews DIR]\n"
        "                    [--web-port PORT | --no-web] [--backend auto|spi|jack|simulated] [--seconds S] [--name CLIENT]\n"
        "                    [--loopback FRAMES] [--script 'TRANSFER:STATUS,DATA1,DATA2;...']\n"
        "         run on the Move: Melodee (its sample library in --samples, by default the Move's own; its\n"
        "         projects in --home/Projects, by default /data/UserData/Melodee; its synths Schwung's\n"
        "         modules in --modules, by default /data/UserData/schwung/modules), or the\n"
        "         spike (--previews: Melodee's pages crushed). spi is the Move's own device on Ableton's firmware,\n"
        "         jack is jackd -d move on Armbian, simulated a stand-in for the device (--loopback, --script).\n"
        "         Melodee's web page: http://move.local:8080 (--web-port: the first port to try), off with --no-web\n"
        "       melodee-move --ctl 'COMMAND' [--home DIR | --control SOCKET]  a command for the running one:\n"
        "                                              status, melodee-press shift+step1, melodee-render serial\n"
        "       melodee-move --self-test FILE.wav       render offline, fail if silent\n"
        "       melodee-move --preview DIR              the screen as PNGs\n"
        "       melodee-move --melodee-previews DIR [--modules DIR]  Melodee's screens as PNGs\n"
        "       melodee-move --probe-modules DIR [PICTURES]  load and play each of Schwung's synths in DIR;\n"
        "                                              PICTURES: their own pages, drawn there\n"
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
    // A blocked signal stays blocked through exec, and whoever started
    // melodee-move may have blocked some: the Move's MoveLauncher blocks
    // INT, USR1 and TERM, so started at power-on (module/boot-entry.sh)
    // Melodee would never hear a stop. None is blocked here, and the threads
    // started from here on inherit that.
    sigset_t none;
    sigemptyset(&none);
    pthread_sigmask(SIG_SETMASK, &none, nullptr);
    // A write to a closed pipe or socket is an error to its writer, not the
    // end of the process: Schwung's modules stream (webstream, radiogarden)
    // and one dying stream must not take Melodee with it.
    std::signal(SIGPIPE, SIG_IGN);
    try {
        std::string previews, samples, home, modules, melodee_previews, which = "melodee", ctl;
        int web_port = web::default_port; // -1: no web page
        move::RunOptions options;
        for (int n = 1; n < argc; ++n) {
            std::string argument = argv[n];
            bool has_value = n + 1 < argc;
            if (argument == "--self-test" && has_value) { SpikeApp app; return move::self_test(app, argv[n + 1]); }
            if (argument == "--preview" && has_value) { SpikeApp app; return move::write_previews(app, argv[n + 1]); }
            if (argument == "--crush" && n + 2 < argc) return move::crush_pictures(argv[n + 1], argv[n + 2]);
            if (argument == "--ui-mockups" && has_value) return ui::write_mockups(argv[n + 1]);
            if (argument == "--test-loop" && has_value) return move::write_test_loop(argv[n + 1]) ? 0 : 1;

            if (argument == "--probe-modules" && has_value) return app::probe_modules(argv[n + 1], n + 2 < argc ? argv[n + 2] : "");
            if (argument == "--melodee-previews" && has_value) melodee_previews = argv[++n];
            else if (argument == "--previews" && has_value) previews = argv[++n];
            else if (argument == "--samples" && has_value) samples = argv[++n];
            else if (argument == "--home" && has_value) home = argv[++n];
            else if (argument == "--modules" && has_value) modules = argv[++n];
            else if (argument == "--app" && has_value) which = argv[++n];
            else if (argument == "--web-port" && has_value) web_port = std::stoi(argv[++n]);
            else if (argument == "--no-web") web_port = -1;
            else if (argument == "--seconds" && has_value) options.seconds = std::stod(argv[++n]);
            else if (argument == "--name" && has_value) options.client_name = argv[++n];
            else if (argument == "--backend" && has_value) options.backend = backend_named(argv[++n]);
            else if (argument == "--loopback" && has_value) options.simulated_loopback_frames = unsigned(std::stoul(argv[++n]));
            else if (argument == "--script" && has_value) options.simulated_script = argv[++n];
            else if (argument == "--control" && has_value) options.control_path = argv[++n];
            else if (argument == "--ctl" && has_value) ctl = argv[++n];
            else { usage(); return 2; }
        }
        // A command for the running melodee-move (move/remote.h), over its socket in Melodee's folder.
        if (!ctl.empty()) return move::send_command(options.control_path.empty() ? app::control_path(home) : options.control_path, ctl);
        if (!melodee_previews.empty()) return app::write_previews(melodee_previews, modules);
        if (which == "spike") {
            SpikeApp app(previews);
            return move::run_app(app, options);
        }
        if (which != "melodee") { usage(); return 2; }
        auto app = std::make_unique<app::MelodeeApp>(samples, home, modules);
        if (options.control_path.empty()) options.control_path = app::control_path(home);
        if (web_port < 0) return move::run_app(*app, options);
        // The web page around it (src/web/web_layer.h), on Melodee's folder: the one its projects are in.
        web::WebLayer layer(*app, {app->studio().home_folder(), uint16_t(web_port)});
        return move::run_app(layer, options);
    } catch (const std::exception& error) {
        std::cerr << "melodee-move: " << error.what() << '\n';
        return 1;
    }
}

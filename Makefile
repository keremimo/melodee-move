# SPDX-License-Identifier: AGPL-3.0-only
# melodee-move: the Move runtime (src/move/), the spike app (src/spike/) and
# their tests, built for linux/arm64 in tools/Dockerfile (./build.sh).
TARGET := build/melodee-move
.DEFAULT_GOAL := all
TESTS := build/move-tests
# The sequencer (docs/UI.md, "Sequencer"): its pattern edits and its keys
# through the app, apart from move-tests.
SEQUENCER_TESTS := build/sequencer-tests
# The controls map (docs/MOVE_CONTROLS.md) against its table, alone: it
# needs only src/app/bindings.*.
CONTROLS_TESTS := build/controls-tests
# The mix and its effects (src/app/mixing.*, fx_host.*), on a whole studio.
MIXER_TESTS := build/mixer-tests
MIDI_TESTS := build/midi-tests
# The pad layouts (src/app/pad_layout.*), alone.
PAD_TESTS := build/pad-layout-tests
# The pad layouts through the Move app: every pad job in both layouts.
PADS_APP_TESTS := build/pads-app-tests
JUCE := build/deps/JUCE/modules
# libjack is loaded at run time by the JACK host (src/move/jack_host.cpp), so
# only its headers are needed here and the binary runs where there is no JACK.
PACKAGES := freetype2 fontconfig
CPPFLAGS := -Isrc -I$(JUCE) $(shell pkg-config --cflags $(PACKAGES) jack) \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DNDEBUG \
    -DJUCE_USE_MP3AUDIOFORMAT=1 -DJUCE_USE_WINDOWS_MEDIA_FORMAT=0 -DJUCE_USE_OPUS=0
# The Move's CM4 is a Cortex-A72, ARMv8.0-A. Never tune for the MPC Sample's
# Cortex-A55 (ARMv8.2-A): its atomics and dot-product instructions do not
# exist on the A72 and stop the program with SIGILL.
ARCH := -mcpu=cortex-a72
CXXFLAGS := -std=c++17 -O2 $(ARCH) -pthread -MMD -MP
CFLAGS := -O2 $(ARCH) -MMD -MP
WARNINGS := -Wall -Wextra -Werror
# The C++ runtime is linked in: Ableton's firmware has an older libstdc++
# (GLIBCXX_3.4.29) than any toolchain builds against, and glibc 2.35 is the
# newest it has, which is why tools/Dockerfile is Ubuntu 22.04.
#
# schwung_move_info is exported for Schwung's modules, which look for it in
# the process (src/app/module_host.cpp).
LDFLAGS := -static-libstdc++ -static-libgcc -Wl,--export-dynamic-symbol=schwung_move_info
LDLIBS := $(shell pkg-config --libs $(PACKAGES)) -ldl -pthread -lrt -lm

# Melodee's own engine and model, compiled unchanged from a melodee-sample
# checkout (MELODEE; ./build.sh mounts ../melodee-sample there) into a
# library, of which the linker takes only what is used. Melodee is changed
# in melodee-sample, never here.
MELODEE ?= ../melodee-sample
MELODEE_INCLUDES := -I$(MELODEE)/src -I$(MELODEE)/base/src
# shell/sample_decode.cpp: Melodee's own reader for WAV, and AIFF, FLAC, Ogg
# and MP3 through JUCE, as on the MPC.
# Kit depth: shell/sample_cache.cpp (long MP3, FLAC, Ogg and AIFF decoded once
# into Melodee/Cache as WAVs, read from storage as they play), project_kits.cpp
# (Load Kit, bank files) and the MPC drum programs' readers. shell/midi_route.cpp
# and mtc_in.cpp: where MIDI in goes, and MTC in (src/app/midi.*).
MELODEE_SOURCES := $(wildcard $(MELODEE)/src/melodee/engine/*.cpp $(MELODEE)/src/melodee/model/*.cpp) \
    $(MELODEE)/src/melodee/shell/sample_decode.cpp $(MELODEE)/src/melodee/shell/sample_cache.cpp \
    $(addprefix $(MELODEE)/src/melodee/shell/,project_kits.cpp mpc_program.cpp mpc_project.cpp mpc_wav_slices.cpp) \
    $(MELODEE)/src/melodee/shell/midi_route.cpp $(MELODEE)/src/melodee/shell/mtc_in.cpp
# web/web_server.cpp and web/card_files.cpp: the helpers of Melodee's page on
# the MPC (JSON text, safe names, paths kept in a folder, file types), plain
# C++, for the page on the Move (src/web/).
MELODEE_SOURCES += $(MELODEE)/src/melodee/web/web_server.cpp $(MELODEE)/src/melodee/web/card_files.cpp
MELODEE_OBJECTS := $(patsubst $(MELODEE)/src/%.cpp,build/obj/melodee-sample/%.o,$(MELODEE_SOURCES))
MELODEE_LIBRARY := build/melodee.a
CPPFLAGS += $(MELODEE_INCLUDES)

MODULE_CPP := $(wildcard $(JUCE)/juce_core/*.cpp $(JUCE)/juce_events/*.cpp $(JUCE)/juce_graphics/*.cpp \
    $(JUCE)/juce_audio_basics/*.cpp) $(JUCE)/juce_audio_formats/juce_audio_formats.cpp
MODULE_C := $(wildcard $(JUCE)/juce_core/*.c $(JUCE)/juce_graphics/*.c) \
    $(addprefix $(JUCE)/juce_audio_formats/juce_audio_formats_,flac_1.c flac_2.c ogg.c)
JUCE_OBJECTS := $(addprefix build/obj/juce/,$(notdir $(MODULE_CPP:.cpp=.o) $(MODULE_C:.c=.o)))
# QuickJS (tools/dependencies.py), for Schwung modules' page scripts: built
# as its own Makefile builds it, with its std and os modules.
QUICKJS := build/deps/quickjs
QUICKJS_SOURCES := quickjs.c dtoa.c libregexp.c libunicode.c cutils.c quickjs-libc.c
QUICKJS_OBJECTS := $(addprefix build/obj/quickjs/,$(QUICKJS_SOURCES:.c=.o))
# NDEBUG: a page script's object left over when its runtime is freed is a
# leak, never an abort of the whole program.
QUICKJS_CFLAGS := -O2 $(ARCH) -funsigned-char -fwrapv -D_GNU_SOURCE -DCONFIG_VERSION=\"2025-04-26\" -DNDEBUG -w
CPPFLAGS += -isystem $(QUICKJS)
build/obj/quickjs/%.o: $(QUICKJS)/%.c
	@mkdir -p $(@D)
	$(CC) $(QUICKJS_CFLAGS) -c $< -o $@
SHARED_OBJECTS := $(patsubst src/%.cpp,build/obj/%.o,$(wildcard src/move/*.cpp src/ui/*.cpp src/spike/*.cpp src/app/*.cpp src/app/screens/*.cpp src/web/*.cpp))

# Melodee's web page (src/web/): its files, with the guide and Rubik, in one
# bundle the binary carries (tools/web_bundle.py, .incbin in web_bundle.cpp),
# and the version it shows, module/module.json's.
WEB_BUNDLE := build/gen/web_bundle.bin
VERSION := $(shell sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' module/module.json)
$(WEB_BUNDLE): tools/web_bundle.py $(wildcard src/web/static/*) docs/GUIDE.md
	@mkdir -p $(@D)
	MELODEE=$(MELODEE) python3 tools/web_bundle.py $@
build/obj/web/web_bundle.o: $(WEB_BUNDLE)
build/obj/web/web_page.o: CPPFLAGS += -DMELODEE_MOVE_VERSION=\"v$(VERSION)\"
WEB_TESTS := build/web-tests
$(WEB_TESTS): build/obj/tests/web_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

# A stand-in Schwung module for the tests, where Schwung keeps its synths.
FAKE_MODULE := build/test-modules/sound_generators/fake
# And a stand-in audio effect, where Schwung keeps them (<id>/<id>.so).
FAKE_FX := build/test-modules/audio_fx/fake-fx
all: $(TARGET) $(TESTS) $(WEB_TESTS) $(SEQUENCER_TESTS) $(CONTROLS_TESTS) $(MIDI_TESTS) $(MIXER_TESTS) $(PAD_TESTS) $(PADS_APP_TESTS) $(FAKE_MODULE)/dsp.so $(FAKE_MODULE)/module.json $(FAKE_MODULE)/page.js \
    $(FAKE_FX)/fake-fx.so $(FAKE_FX)/module.json
$(FAKE_FX)/fake-fx.so: tests/fake_fx/fake-fx.c
	@mkdir -p $(@D)
	$(CC) -O2 -fPIC -shared -Wall -Wextra -Werror $< -o $@
$(FAKE_FX)/module.json: tests/fake_fx/module.json
	@mkdir -p $(@D)
	cp $< $@
$(FAKE_MODULE)/dsp.so: tests/fake_module/dsp.c
	@mkdir -p $(@D)
	$(CC) -O2 -fPIC -shared -Wall -Wextra -Werror $< -o $@
$(FAKE_MODULE)/module.json: tests/fake_module/module.json
	@mkdir -p $(@D)
	cp $< $@
$(FAKE_MODULE)/page.js: tests/fake_module/page.js
	@mkdir -p $(@D)
	cp $< $@
$(TARGET): build/obj/main.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(TESTS): build/obj/tests/move_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(MIXER_TESTS): build/obj/tests/mixer_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(SEQUENCER_TESTS): build/obj/tests/sequencer_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(MIDI_TESTS): build/obj/tests/midi_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
# Undo, projects and the system (tests/projects_tests.cpp), a binary of
# their own.
PROJECTS_TESTS := build/projects-tests
all: $(PROJECTS_TESTS)
$(PROJECTS_TESTS): build/obj/tests/projects_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(CONTROLS_TESTS): build/obj/tests/controls_tests.o build/obj/app/bindings.o
	$(CXX) -static-libstdc++ -static-libgcc $^ -pthread -o $@
$(PAD_TESTS): build/obj/tests/pad_layout_tests.o build/obj/app/pad_layout.o
	$(CXX) -static-libstdc++ -static-libgcc $^ -pthread -o $@
$(PADS_APP_TESTS): build/obj/tests/pads_app_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(QUICKJS_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(MELODEE_LIBRARY): $(MELODEE_OBJECTS)
	rm -f $@ && $(AR) rcs $@ $^
build/obj/melodee-sample/%.o: $(MELODEE)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -w -c $< -o $@
build/obj/tests/%.o: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(WARNINGS) -c $< -o $@
build/obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(WARNINGS) -c $< -o $@
define module_rule
build/obj/juce/$(notdir $(1:.cpp=.o)): $(1)
	@mkdir -p $$(@D)
	$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) -w -c $$< -o $$@
endef
$(foreach m,$(MODULE_CPP),$(eval $(call module_rule,$(m))))
define c_module_rule
build/obj/juce/$(notdir $(1:.c=.o)): $(1)
	@mkdir -p $$(@D)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -w -c $$< -o $$@
endef
$(foreach m,$(MODULE_C),$(eval $(call c_module_rule,$(m))))

juce: $(JUCE_OBJECTS)
.PHONY: all clean juce
clean:
	rm -rf build/obj $(TARGET) $(TESTS) $(PROJECTS_TESTS) $(WEB_TESTS) $(SEQUENCER_TESTS) $(CONTROLS_TESTS) $(MIDI_TESTS) $(MIXER_TESTS) $(PAD_TESTS) $(PADS_APP_TESTS)
-include $(shell find build/obj -name '*.d' 2>/dev/null)

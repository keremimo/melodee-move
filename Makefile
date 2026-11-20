# SPDX-License-Identifier: AGPL-3.0-only
# melodee-move: the Move runtime (src/move/), the spike app (src/spike/) and
# their tests, built for linux/arm64 in tools/Dockerfile (./build.sh).
TARGET := build/melodee-move
TESTS := build/move-tests
JUCE := build/deps/JUCE/modules
# libjack is loaded at run time by the JACK host (src/move/jack_host.cpp), so
# only its headers are needed here and the binary runs where there is no JACK.
PACKAGES := freetype2 fontconfig
CPPFLAGS := -Isrc -I$(JUCE) $(shell pkg-config --cflags $(PACKAGES) jack) \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DNDEBUG
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
LDFLAGS := -static-libstdc++ -static-libgcc
LDLIBS := $(shell pkg-config --libs $(PACKAGES)) -ldl -pthread -lrt

MODULE_CPP := $(wildcard $(JUCE)/juce_core/*.cpp $(JUCE)/juce_events/*.cpp $(JUCE)/juce_graphics/*.cpp)
MODULE_C := $(wildcard $(JUCE)/juce_core/*.c $(JUCE)/juce_graphics/*.c)
JUCE_OBJECTS := $(addprefix build/obj/juce/,$(notdir $(MODULE_CPP:.cpp=.o) $(MODULE_C:.c=.o)))
SHARED_OBJECTS := $(patsubst src/%.cpp,build/obj/%.o,$(wildcard src/move/*.cpp src/spike/*.cpp))

all: $(TARGET) $(TESTS)
$(TARGET): build/obj/main.o $(SHARED_OBJECTS) $(JUCE_OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(TESTS): build/obj/tests/move_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
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
	rm -rf build/obj $(TARGET) $(TESTS)
-include $(shell find build/obj -name '*.d' 2>/dev/null)

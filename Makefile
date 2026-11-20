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

# Melodee's own engine and model, compiled unchanged from a melodee-sample
# checkout (MELODEE; ./build.sh mounts ../melodee-sample there) into a
# library, of which the linker takes only what is used. Melodee is changed
# in melodee-sample, never here.
MELODEE ?= ../melodee-sample
MELODEE_INCLUDES := -I$(MELODEE)/src -I$(MELODEE)/base/src
MELODEE_SOURCES := $(wildcard $(MELODEE)/src/melodee/engine/*.cpp $(MELODEE)/src/melodee/model/*.cpp)
MELODEE_OBJECTS := $(patsubst $(MELODEE)/src/%.cpp,build/obj/melodee-sample/%.o,$(MELODEE_SOURCES))
MELODEE_LIBRARY := build/melodee.a
CPPFLAGS += $(MELODEE_INCLUDES)

MODULE_CPP := $(wildcard $(JUCE)/juce_core/*.cpp $(JUCE)/juce_events/*.cpp $(JUCE)/juce_graphics/*.cpp)
MODULE_C := $(wildcard $(JUCE)/juce_core/*.c $(JUCE)/juce_graphics/*.c)
JUCE_OBJECTS := $(addprefix build/obj/juce/,$(notdir $(MODULE_CPP:.cpp=.o) $(MODULE_C:.c=.o)))
SHARED_OBJECTS := $(patsubst src/%.cpp,build/obj/%.o,$(wildcard src/move/*.cpp src/ui/*.cpp src/spike/*.cpp src/app/*.cpp))

all: $(TARGET) $(TESTS)
$(TARGET): build/obj/main.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(TESTS): build/obj/tests/move_tests.o $(SHARED_OBJECTS) $(JUCE_OBJECTS) $(MELODEE_LIBRARY)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(MELODEE_LIBRARY): $(MELODEE_OBJECTS)
	rm -f $@ && $(AR) rcs $@ $^
build/obj/melodee-sample/%.o: $(MELODEE)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(MELODEE_INCLUDES) -DNDEBUG $(CXXFLAGS) -w -c $< -o $@
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

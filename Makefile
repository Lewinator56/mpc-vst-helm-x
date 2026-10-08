# ==============================================================================
# HelmX - MPC Standalone VST Plugin Makefile
# ==============================================================================

CROSS_COMPILE ?= arm-linux-gnueabihf-
CC            := $(CROSS_COMPILE)gcc
CXX           := $(CROSS_COMPILE)g++
STRIP         := $(CROSS_COMPILE)strip

BUILD_DIR     ?= build
TARGET        := $(BUILD_DIR)/helm.so
PACKAGE_NAME  := Lewinator56 - VST - HelmX
PACKAGE_DIR   := $(BUILD_DIR)/package/$(PACKAGE_NAME)

INCLUDES      := -Isrc -Isrc/dsp/mopo -Isrc/dsp/synthesis -Isrc/dsp/common -I$(BUILD_DIR) -Iwrapper
COMMON_FLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -Wno-ignored-qualifiers -Wno-sign-compare
CFLAGS        ?= $(COMMON_FLAGS) -std=gnu11
CXXFLAGS      ?= $(COMMON_FLAGS) -std=gnu++11
LDFLAGS       ?= -shared -fPIC -fvisibility=hidden -Wl,--no-undefined
LIBS          ?= -lm -lpthread -ldl

# C++ DSP & Engine Sources
CPP_SRCS := \
	src/helm_adapter.cpp \
	src/dsp/common/helm_common.cpp \
	src/dsp/mopo/alias.cpp \
	src/dsp/mopo/arpeggiator.cpp \
	src/dsp/mopo/biquad_filter.cpp \
	src/dsp/mopo/bit_crush.cpp \
	src/dsp/mopo/bypass_router.cpp \
	src/dsp/mopo/delay.cpp \
	src/dsp/mopo/distortion.cpp \
	src/dsp/mopo/envelope.cpp \
	src/dsp/mopo/feedback.cpp \
	src/dsp/mopo/formant_manager.cpp \
	src/dsp/mopo/ladder_filter.cpp \
	src/dsp/mopo/linear_slope.cpp \
	src/dsp/mopo/magnitude_lookup.cpp \
	src/dsp/mopo/memory.cpp \
	src/dsp/mopo/midi_lookup.cpp \
	src/dsp/mopo/mono_panner.cpp \
	src/dsp/mopo/operators.cpp \
	src/dsp/mopo/oscillator.cpp \
	src/dsp/mopo/portamento_slope.cpp \
	src/dsp/mopo/processor.cpp \
	src/dsp/mopo/processor_router.cpp \
	src/dsp/mopo/resonance_lookup.cpp \
	src/dsp/mopo/reverb.cpp \
	src/dsp/mopo/reverb_all_pass.cpp \
	src/dsp/mopo/reverb_comb.cpp \
	src/dsp/mopo/sample_decay_lookup.cpp \
	src/dsp/mopo/simple_delay.cpp \
	src/dsp/mopo/smooth_filter.cpp \
	src/dsp/mopo/smooth_value.cpp \
	src/dsp/mopo/state_variable_filter.cpp \
	src/dsp/mopo/step_generator.cpp \
	src/dsp/mopo/stutter.cpp \
	src/dsp/mopo/trigger_operators.cpp \
	src/dsp/mopo/value.cpp \
	src/dsp/mopo/voice_handler.cpp \
	src/dsp/synthesis/dc_filter.cpp \
	src/dsp/synthesis/detune_lookup.cpp \
	src/dsp/synthesis/fixed_point_oscillator.cpp \
	src/dsp/synthesis/fixed_point_wave.cpp \
	src/dsp/synthesis/gate.cpp \
	src/dsp/synthesis/helm_engine.cpp \
	src/dsp/synthesis/helm_equalizer.cpp \
	src/dsp/synthesis/helm_lfo.cpp \
	src/dsp/synthesis/helm_module.cpp \
	src/dsp/synthesis/helm_oscillators.cpp \
	src/dsp/synthesis/helm_voice_handler.cpp \
	src/dsp/synthesis/noise_oscillator.cpp \
	src/dsp/synthesis/peak_meter.cpp \
	src/dsp/synthesis/resonance_cancel.cpp \
	src/dsp/synthesis/trigger_random.cpp \
	src/dsp/synthesis/value_switch.cpp

# C Framebuffer & Wrapper Sources
C_SRCS := \
	src/mpc_framebuffer.c \
	src/mpc_eq_ui.c \
	wrapper/vst2_wrap.c

# Object mapping mirroring source tree inside $(BUILD_DIR)/obj/
CPP_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/obj/%.o,$(CPP_SRCS))
C_OBJS   := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(C_SRCS))
OBJS     := $(CPP_OBJS) $(C_OBJS)

.PHONY: all package clean distclean

all: $(TARGET) package

$(BUILD_DIR)/params.h: vst.json params.json
	@mkdir -p $(BUILD_DIR)
	python3 tools/gen_vst.py vst.json

$(OBJS): | $(BUILD_DIR)/params.h

$(BUILD_DIR)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	@echo "  LINK $@"
	@$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
	@$(STRIP) $@
	@echo "Successfully built $@"
	@ls -lh $@

package: $(TARGET)
	@mkdir -p "$(PACKAGE_DIR)"
	@cp -f $(TARGET) "$(PACKAGE_DIR)/"
	@[ -f fb_timing.txt ] && cp -f fb_timing.txt "$(PACKAGE_DIR)/" || true
	@[ -d patches ] && cp -rf patches "$(PACKAGE_DIR)/" || true
	@echo "Plugin packaged to $(PACKAGE_DIR)/"

clean:
	rm -rf $(BUILD_DIR)/obj $(TARGET) "$(PACKAGE_DIR)/helm.so"

distclean:
	rm -rf $(BUILD_DIR)

/* Copyright 2013-2017 Matt Tytel
 *
 * helm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * helm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with helm.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "helm_oscillators.h"

#include "detune_lookup.h"

#define RAND_DECAY 0.999

namespace mopo {
  const mopo_float HelmOscillators::scales[] = {
      1.0, 1.0,
      sqrt(1.0 / 2.0), sqrt(1.0 / 2.0),
      sqrt(1.0 / 3.0), sqrt(1.0 / 3.0),
      sqrt(1.0 / 4.0), sqrt(1.0 / 4.0),
      sqrt(1.0 / 5.0), sqrt(1.0 / 5.0),
      sqrt(1.0 / 6.0), sqrt(1.0 / 6.0),
      sqrt(1.0 / 7.0), sqrt(1.0 / 7.0),
      sqrt(1.0 / 8.0), sqrt(1.0 / 8.0),
  };

  HelmOscillators::HelmOscillators() : Processor(kNumInputs, 4) {
    utils::zeroBuffer(oscillator1_cross_mods_, MAX_BUFFER_SIZE + 1);
    utils::zeroBuffer(oscillator2_cross_mods_, MAX_BUFFER_SIZE + 1);

    oscillator1_phase_base_ = 0.0;
    oscillator2_phase_base_ = 0.0;

    for (int v = 0; v < MAX_UNISON; ++v) {
      oscillator1_phases_[v] = 0;
      oscillator2_phases_[v] = 0;
      wave_buffers1_[v] = nullptr;
      wave_buffers2_[v] = nullptr;
      detune_diffs1_[v] = 0;
      detune_diffs2_[v] = 0;
      gains_left1_[v] = 1.0f;
      gains_right1_[v] = 1.0f;
      gains_left2_[v] = 1.0f;
      gains_right2_[v] = 1.0f;
    }

    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
      oscillator1_phase_diffs_[i] = 0;
      oscillator2_phase_diffs_[i] = 0;
    }
  }

  void HelmOscillators::reset(int i) {
    oscillator1_cross_mods_[i] = 0;
    oscillator2_cross_mods_[i] = 0;
    oscillator1_cross_mods_[i + 1] = 0;
    oscillator2_cross_mods_[i + 1] = 0;

    oscillator1_phase_base_ = 0.0;
    oscillator2_phase_base_ = 0.0;
    oscillator1_phases_[0] = 0;
    oscillator2_phases_[0] = 0;

    for (int u = 1; u < MAX_UNISON; ++u) {
      oscillator1_phases_[u] = (UINT_MAX / RAND_MAX) * rand();
      oscillator2_phases_[u] = (UINT_MAX / RAND_MAX) * rand();
    }
  }

  void HelmOscillators::loadBasePhaseInc() {
    int samples = buffer_size_;

    int* dest1 = oscillator1_phase_diffs_;
    int* dest2 = oscillator2_phase_diffs_;

    const mopo_float* src1 = input(kOscillator1PhaseInc)->source->buffer;
    const mopo_float* src2 = input(kOscillator2PhaseInc)->source->buffer;

    VECTORIZE_LOOP
    for (int i = 0; i < samples; ++i) {
      dest1[i] = (int)((unsigned int)(UINT_MAX * src1[i]));
      dest2[i] = (int)((unsigned int)(UINT_MAX * src2[i]));
    }

    for (int i = 1; i < samples; ++i) {
      dest1[i] = (int)((unsigned int)dest1[i] + (unsigned int)dest1[i - 1]);
      dest2[i] = (int)((unsigned int)dest2[i] + (unsigned int)dest2[i - 1]);
    }
  }

  void HelmOscillators::computeDetuneRatios(int* detune_diffs,
                                            int oscillator_diff,
                                            bool harmonize, mopo_float detune,
                                            int voices) {
    int harmonize_mult = harmonize ? 1 : 0;
    for (int v = 0; v < MAX_UNISON; ++v) {
      mopo_float amount = (detune * ((v + 1) / 2)) / ((voices + 1) / 2);

      if (v % 2)
        amount = -amount;

      mopo_float harmonic = harmonize_mult * v;

      mopo_float detune_ratio = harmonic + DetuneLookup::detuneLookup(amount);
      detune_diffs[v] = detune_ratio * oscillator_diff - oscillator_diff;
    }
  }

  void HelmOscillators::computePanningGains(mopo_float* gains_left,
                                           mopo_float* gains_right,
                                           mopo_float pan,
                                           mopo_float spread,
                                           int voices) {
    pan = utils::clamp(pan, -1.0f, 1.0f);
    spread = utils::clamp(spread, 0.0f, 1.0f);

    for (int v = 0; v < MAX_UNISON; ++v) {
      mopo_float pan_v = pan;
      if (voices > 1 && v > 0) {
        mopo_float spread_offset = (spread * ((v + 1) / 2)) / ((voices + 1.0f) / 2.0f);
        if (v % 2)
          spread_offset = -spread_offset;
        pan_v = utils::clamp(pan + spread_offset, -1.0f, 1.0f);
      }
      if (pan_v <= 0.0f) {
        gains_left[v] = 1.0f;
        gains_right[v] = 1.0f + pan_v;
      } else {
        gains_left[v] = 1.0f - pan_v;
        gains_right[v] = 1.0f;
      }
    }
  }

  void HelmOscillators::prepareBuffers(mopo_float** wave_buffers,
                                       const int* detune_diffs,
                                       const int* oscillator_phase_diffs,
                                       int waveform) {
    for (int v = 0; v < MAX_UNISON; ++v) {
      int phase_diff = detune_diffs[v] + oscillator_phase_diffs[0];
      wave_buffers[v] = FixedPointWave::getBuffer(waveform, phase_diff);
    }
  }

  void HelmOscillators::processInitial() {
    loadBasePhaseInc();

    int voices1 = utils::iclamp(input(kUnisonVoices1)->source->buffer[0], 1, MAX_UNISON);
    int voices2 = utils::iclamp(input(kUnisonVoices2)->source->buffer[0], 1, MAX_UNISON);
    mopo_float detune1 = input(kUnisonDetune1)->source->buffer[0];
    mopo_float detune2 = input(kUnisonDetune2)->source->buffer[0];
    mopo_float harmonize1 = input(kHarmonize1)->source->buffer[0];
    mopo_float harmonize2 = input(kHarmonize2)->source->buffer[0];

    mopo_float pan1 = input(kOscillator1Pan)->source->buffer[0];
    mopo_float pan2 = input(kOscillator2Pan)->source->buffer[0];
    mopo_float spread1 = input(kUnisonSpread1)->source->buffer[0];
    mopo_float spread2 = input(kUnisonSpread2)->source->buffer[0];

    computeDetuneRatios(detune_diffs1_, oscillator1_phase_diffs_[0],
                        harmonize1, detune1, voices1);
    computeDetuneRatios(detune_diffs2_, oscillator2_phase_diffs_[0],
                        harmonize2, detune2, voices2);

    computePanningGains(gains_left1_, gains_right1_, pan1, spread1, voices1);
    computePanningGains(gains_left2_, gains_right2_, pan2, spread2, voices2);

    int wave1 = static_cast<int>(input(kOscillator1Waveform)->source->buffer[0] + 0.5);
    int wave2 = static_cast<int>(input(kOscillator2Waveform)->source->buffer[0] + 0.5);
    wave1 = utils::iclamp(wave1, 0, FixedPointWaveLookup::kWhiteNoise - 1);
    wave2 = utils::iclamp(wave2, 0, FixedPointWaveLookup::kWhiteNoise - 1);

    prepareBuffers(wave_buffers1_, detune_diffs1_, oscillator1_phase_diffs_, wave1);
    prepareBuffers(wave_buffers2_, detune_diffs2_, oscillator2_phase_diffs_, wave2);
  }

  void HelmOscillators::processCrossMod() {
    mopo_float cross_mod = input(kCrossMod)->at(0);
    const int* phase_diffs1 = oscillator1_phase_diffs_;
    const int* phase_diffs2 = oscillator2_phase_diffs_;
    int* dest_cross_mod2 = oscillator2_cross_mods_;
    int* dest_cross_mod1 = oscillator1_cross_mods_;

    if (cross_mod == 0.0) {
      utils::zeroBuffer(dest_cross_mod1, buffer_size_);
      utils::zeroBuffer(dest_cross_mod2, buffer_size_);
      return;
    }

    int i = 0;
    if (input(kReset)->source->triggered) {
      int trigger_offset = input(kReset)->source->trigger_offset;
      for (; i < trigger_offset; ++i) {
        tickCrossMod(i, cross_mod, dest_cross_mod1, dest_cross_mod2,
                     oscillator1_phase_base_ + phase_diffs1[i],
                     oscillator2_phase_base_ + phase_diffs2[i]);
      }

      oscillator1_cross_mods_[i] = 0;
      oscillator2_cross_mods_[i] = 0;
      oscillator1_cross_mods_[i + 1] = 0;
      oscillator2_cross_mods_[i + 1] = 0;

      oscillator1_phase_base_ = 0.0;
      oscillator2_phase_base_ = 0.0;
    }
    for (; i < buffer_size_; ++i) {
      tickCrossMod(i, cross_mod, dest_cross_mod1, dest_cross_mod2,
                   oscillator1_phase_base_ + phase_diffs1[i],
                   oscillator2_phase_base_ + phase_diffs2[i]);
    }
  }

  void HelmOscillators::processVoices() {
    int voices1 = utils::iclamp(input(kUnisonVoices1)->source->buffer[0], 1, MAX_UNISON);
    int voices2 = utils::iclamp(input(kUnisonVoices2)->source->buffer[0], 1, MAX_UNISON);

    utils::zeroBuffer(oscillator1_left_totals_, buffer_size_);
    utils::zeroBuffer(oscillator1_right_totals_, buffer_size_);
    utils::zeroBuffer(oscillator2_left_totals_, buffer_size_);
    utils::zeroBuffer(oscillator2_right_totals_, buffer_size_);

    int j = 0;
    if (input(kReset)->source->triggered) {
      int trigger_offset = input(kReset)->source->trigger_offset;
      for (; j < trigger_offset; ++j)
        tickInitialVoices(j);

      oscillator1_phases_[0] = 0;
      oscillator2_phases_[0] = 0;
    }

    for (; j < buffer_size_; ++j)
      tickInitialVoices(j);

    for (int v = 1; v < voices1; ++v) {
      const mopo_float* wave_buffer = wave_buffers1_[v];
      unsigned int start_phase = oscillator1_phases_[v];
      int detune = detune_diffs1_[v];

      int i = 0;
      if (input(kReset)->source->triggered) {
        int trigger_offset = input(kReset)->source->trigger_offset;
        for (; i < trigger_offset; ++i)
          tickVoice1(i, v, wave_buffer, start_phase, detune);

        oscillator1_phases_[v] = (UINT_MAX / RAND_MAX) * rand();
      }

      for (; i < buffer_size_; ++i)
        tickVoice1(i, v, wave_buffer, start_phase, detune);
    }

    for (int v = 1; v < voices2; ++v) {
      const mopo_float* wave_buffer = wave_buffers2_[v];
      unsigned int start_phase = oscillator2_phases_[v];
      int detune = detune_diffs2_[v];

      int i = 0;
      if (input(kReset)->source->triggered) {
        int trigger_offset = input(kReset)->source->trigger_offset;
        for (; i < trigger_offset; ++i)
          tickVoice2(i, v, wave_buffer, start_phase, detune);

        oscillator2_phases_[v] = (UINT_MAX / RAND_MAX) * rand();
      }
      for (; i < buffer_size_; ++i)
        tickVoice2(i, v, wave_buffer, start_phase, detune);
    }

    finishVoices(voices1, voices2);
  }

  void HelmOscillators::finishVoices(int voices1, int voices2) {
    mopo_float scale1 = scales[voices1];
    mopo_float scale2 = scales[voices2];

    mopo_float* osc1_out_l = output(0)->buffer;
    mopo_float* osc1_out_r = output(1)->buffer;
    mopo_float* osc2_out_l = output(2)->buffer;
    mopo_float* osc2_out_r = output(3)->buffer;
    const mopo_float* amp1 = input(kOscillator1Amplitude)->source->buffer;
    const mopo_float* amp2 = input(kOscillator2Amplitude)->source->buffer;
    const mopo_float* osc1_l = oscillator1_left_totals_;
    const mopo_float* osc1_r = oscillator1_right_totals_;
    const mopo_float* osc2_l = oscillator2_left_totals_;
    const mopo_float* osc2_r = oscillator2_right_totals_;

    VECTORIZE_LOOP
    for (int j = 0; j < buffer_size_; ++j) {
      osc1_out_l[j] = amp1[j] * scale1 * osc1_l[j];
      osc1_out_r[j] = amp1[j] * scale1 * osc1_r[j];
      osc2_out_l[j] = amp2[j] * scale2 * osc2_l[j];
      osc2_out_r[j] = amp2[j] * scale2 * osc2_r[j];
      MOPO_ASSERT(std::isfinite(osc1_out_l[j]));
      MOPO_ASSERT(std::isfinite(osc1_out_r[j]));
      MOPO_ASSERT(std::isfinite(osc2_out_l[j]));
      MOPO_ASSERT(std::isfinite(osc2_out_r[j]));
    }

    oscillator1_cross_mods_[0] = oscillator1_cross_mods_[buffer_size_];
    oscillator2_cross_mods_[0] = oscillator2_cross_mods_[buffer_size_];

    oscillator1_phase_base_ += oscillator1_phase_diffs_[buffer_size_ - 1];
    oscillator2_phase_base_ += oscillator2_phase_diffs_[buffer_size_ - 1];

    for (int v = 0; v < MAX_UNISON; ++v) {
      oscillator1_phases_[v] += oscillator1_phase_diffs_[buffer_size_ - 1] +
                                buffer_size_ * detune_diffs1_[v];
      oscillator2_phases_[v] += oscillator2_phase_diffs_[buffer_size_ - 1] +
                                buffer_size_ * detune_diffs2_[v];
    }
  }

  void HelmOscillators::process() {
    processInitial();
    processCrossMod();
    processVoices();
  }
} // namespace mopo

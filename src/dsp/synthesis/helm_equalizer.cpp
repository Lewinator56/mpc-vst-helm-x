#include "helm_equalizer.h"
#include <cmath>
#include <cstring>

namespace mopo {

  HelmEqualizer::HelmEqualizer() : Processor(kNumInputs, 2) {
    for (int i = 0; i < 5; ++i) {
      stages_[i].reset();
      stages_[i].b0 = 1.0;
      stages_[i].b1 = stages_[i].b2 = stages_[i].a1 = stages_[i].a2 = 0.0;
      stages_[i].last_freq = -1.0;
      stages_[i].last_gain = -999.0;
      stages_[i].last_q = -1.0;
      stages_[i].last_shelf = -1;
      stages_[i].last_on = 1;
    }
  }

  void HelmEqualizer::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    for (int i = 0; i < 5; ++i) {
      stages_[i].last_freq = -1.0; // Invalidate cache on sample rate change
      stages_[i].reset();
    }
  }

  void HelmEqualizer::updateCoefficients(int b, int shelf, mopo_float freq, mopo_float gain_db, mopo_float q) {
    BiquadStage& st = stages_[b];
    if (st.last_shelf == shelf &&
        std::abs(st.last_freq - freq) < 0.01 &&
        std::abs(st.last_gain - gain_db) < 0.01 &&
        std::abs(st.last_q - q) < 0.005) {
      return;
    }

    st.last_shelf = shelf;
    st.last_freq = freq;
    st.last_gain = gain_db;
    st.last_q = q;

    if (freq < 10.0) freq = 10.0;
    if (freq > sample_rate_ * 0.49) freq = sample_rate_ * 0.49;
    if (q < 0.1) q = 0.1;

    mopo_float w0 = 2.0 * PI * freq / sample_rate_;
    mopo_float cos_w = std::cos(w0);
    mopo_float sin_w = std::sin(w0);
    mopo_float A = std::pow(10.0, gain_db / 40.0); // sqrt(linear amplitude)

    mopo_float b0, b1, b2, a0, a1, a2;

    if (shelf == 0 && (b == 0 || b == 4)) {
      mopo_float term = (A + 1.0 / A) * (1.0 / q - 1.0) + 2.0;
      if (term < 0.0) term = 0.0;
      mopo_float alpha = (sin_w / 2.0) * std::sqrt(term);
      mopo_float sq = 2.0 * std::sqrt(A) * alpha;

      if (b == 0) {
        // Low Shelf
        b0 = A * ((A + 1.0) - (A - 1.0) * cos_w + sq);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cos_w);
        b2 = A * ((A + 1.0) - (A - 1.0) * cos_w - sq);
        a0 = (A + 1.0) + (A - 1.0) * cos_w + sq;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cos_w);
        a2 = (A + 1.0) + (A - 1.0) * cos_w - sq;
      } else {
        // High Shelf
        b0 = A * ((A + 1.0) + (A - 1.0) * cos_w + sq);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cos_w);
        b2 = A * ((A + 1.0) + (A - 1.0) * cos_w - sq);
        a0 = (A + 1.0) - (A - 1.0) * cos_w + sq;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cos_w);
        a2 = (A + 1.0) - (A - 1.0) * cos_w - sq;
      }
    } else {
      // Peaking Bell filter
      mopo_float alpha = sin_w / (2.0 * q);
      b0 = 1.0 + alpha * A;
      b1 = -2.0 * cos_w;
      b2 = 1.0 - alpha * A;
      a0 = 1.0 + alpha / A;
      a1 = -2.0 * cos_w;
      a2 = 1.0 - alpha / A;
    }

    mopo_float inv_a0 = 1.0 / a0;
    st.b0 = b0 * inv_a0;
    st.b1 = b1 * inv_a0;
    st.b2 = b2 * inv_a0;
    st.a1 = a1 * inv_a0;
    st.a2 = a2 * inv_a0;
  }

  void HelmEqualizer::process() {
    const mopo_float* in_l = input(kAudioLeft)->source->buffer;
    const mopo_float* in_r = input(kAudioRight)->source->buffer;
    mopo_float* out_l = output(0)->buffer;
    mopo_float* out_r = output(1)->buffer;

    if (input(kOn)->at(0) < 0.5) {
      std::memcpy(out_l, in_l, buffer_size_ * sizeof(mopo_float));
      std::memcpy(out_r, in_r, buffer_size_ * sizeof(mopo_float));
      return;
    }

    std::memcpy(out_l, in_l, buffer_size_ * sizeof(mopo_float));
    std::memcpy(out_r, in_r, buffer_size_ * sizeof(mopo_float));

    // Band 1
    int on1 = input(kBand1On)->at(0) > 0.5;
    if (on1) {
      int shelf1 = (int)std::lround(input(kBand1Shelf)->at(0));
      updateCoefficients(0, shelf1, input(kBand1Frequency)->at(0), input(kBand1Gain)->at(0), input(kBand1Q)->at(0));
    }

    // Band 2
    int on2 = input(kBand2On)->at(0) > 0.5;
    if (on2) {
      updateCoefficients(1, 1, input(kBand2Frequency)->at(0), input(kBand2Gain)->at(0), input(kBand2Q)->at(0));
    }

    // Band 3
    int on3 = input(kBand3On)->at(0) > 0.5;
    if (on3) {
      updateCoefficients(2, 1, input(kBand3Frequency)->at(0), input(kBand3Gain)->at(0), input(kBand3Q)->at(0));
    }

    // Band 4
    int on4 = input(kBand4On)->at(0) > 0.5;
    if (on4) {
      updateCoefficients(3, 1, input(kBand4Frequency)->at(0), input(kBand4Gain)->at(0), input(kBand4Q)->at(0));
    }

    // Band 5
    int on5 = input(kBand5On)->at(0) > 0.5;
    if (on5) {
      int shelf5 = (int)std::lround(input(kBand5Shelf)->at(0));
      updateCoefficients(4, shelf5, input(kBand5Frequency)->at(0), input(kBand5Gain)->at(0), input(kBand5Q)->at(0));
    }

    int active_bands[5] = { on1, on2, on3, on4, on5 };

    // Process cascaded biquads in Direct Form II Transposed
    for (int b = 0; b < 5; ++b) {
      if (!active_bands[b]) continue;
      BiquadStage& st = stages_[b];
      mopo_float b0 = st.b0, b1 = st.b1, b2 = st.b2, a1 = st.a1, a2 = st.a2;
      mopo_float s1_l = st.s1_l, s2_l = st.s2_l;
      mopo_float s1_r = st.s1_r, s2_r = st.s2_r;

      for (int i = 0; i < buffer_size_; ++i) {
        // Left
        mopo_float xl = out_l[i];
        mopo_float yl = b0 * xl + s1_l;
        s1_l = b1 * xl - a1 * yl + s2_l;
        s2_l = b2 * xl - a2 * yl;
        out_l[i] = yl;

        // Right
        mopo_float xr = out_r[i];
        mopo_float yr = b0 * xr + s1_r;
        s1_r = b1 * xr - a1 * yr + s2_r;
        s2_r = b2 * xr - a2 * yr;
        out_r[i] = yr;
      }

      st.s1_l = s1_l; st.s2_l = s2_l;
      st.s1_r = s1_r; st.s2_r = s2_r;
    }
  }

} // namespace mopo

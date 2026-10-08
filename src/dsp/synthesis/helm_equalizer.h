#pragma once
#ifndef HELM_EQUALIZER_H
#define HELM_EQUALIZER_H

#include "processor.h"
#include "utils.h"

namespace mopo {

  class HelmEqualizer : public Processor {
    public:
      enum Inputs {
        kAudioLeft,
        kAudioRight,
        kOn,

        kBand1On,
        kBand1Shelf,
        kBand1Frequency,
        kBand1Gain,
        kBand1Q,

        kBand2On,
        kBand2Frequency,
        kBand2Gain,
        kBand2Q,

        kBand3On,
        kBand3Frequency,
        kBand3Gain,
        kBand3Q,

        kBand4On,
        kBand4Frequency,
        kBand4Gain,
        kBand4Q,

        kBand5On,
        kBand5Shelf,
        kBand5Frequency,
        kBand5Gain,
        kBand5Q,

        kNumInputs
      };

      HelmEqualizer();
      virtual ~HelmEqualizer() { }

      virtual Processor* clone() const override { return new HelmEqualizer(*this); }
      virtual void process() override;
      virtual void setSampleRate(int sample_rate) override;

    private:
      struct BiquadStage {
        mopo_float b0, b1, b2, a1, a2;
        mopo_float s1_l, s2_l;
        mopo_float s1_r, s2_r;

        // Cached parameter values to avoid redundant recomputations
        mopo_float last_freq, last_gain, last_q;
        int last_shelf, last_on;

        void reset() {
          s1_l = s2_l = s1_r = s2_r = 0.0;
        }
      };

      BiquadStage stages_[5];
      void updateCoefficients(int band_idx, int shelf, mopo_float freq, mopo_float gain_db, mopo_float q);
  };

} // namespace mopo

#endif // HELM_EQUALIZER_H

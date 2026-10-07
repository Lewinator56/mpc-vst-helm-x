#pragma once
#ifndef HELM_FILTER_ROUTER_H
#define HELM_FILTER_ROUTER_H

#include "processor.h"
#include "utils.h"

namespace mopo {

  enum FilterRoutingMode {
    kRoutingSeries12 = 0,
    kRoutingParallel,
    kRoutingSeries1Split,
    kRoutingSeries2Split,
    kNumRoutingModes
  };

  class Filter1InputRouter : public Processor {
    public:
      enum Inputs {
        kOsc1Left,
        kOsc1Right,
        kOsc2Left,
        kOsc2Right,
        kSub,
        kNoise,
        kRoutingMode,
        kSubTarget,
        kNoiseTarget,
        kNumInputs
      };

      Filter1InputRouter() : Processor(kNumInputs, 2) { }
      virtual ~Filter1InputRouter() { }

      virtual Processor* clone() const override {
        return new Filter1InputRouter(*this);
      }

      virtual void process() override {
        int mode = utils::iclamp(static_cast<int>(input(kRoutingMode)->at(0) + 0.5f), 0, 3);
        int sub_target = utils::iclamp(static_cast<int>(input(kSubTarget)->at(0) + 0.5f), 0, 2);
        int noise_target = utils::iclamp(static_cast<int>(input(kNoiseTarget)->at(0) + 0.5f), 0, 2);

        const mopo_float* osc1_l = input(kOsc1Left)->source->buffer;
        const mopo_float* osc1_r = input(kOsc1Right)->source->buffer;
        const mopo_float* osc2_l = input(kOsc2Left)->source->buffer;
        const mopo_float* osc2_r = input(kOsc2Right)->source->buffer;
        const mopo_float* sub = input(kSub)->source->buffer;
        const mopo_float* noise = input(kNoise)->source->buffer;

        mopo_float* out_l = output(0)->buffer;
        mopo_float* out_r = output(1)->buffer;

        VECTORIZE_LOOP
        for (int i = 0; i < buffer_size_; ++i) {
          mopo_float sub_f1 = (sub_target == 0 || sub_target == 1) ? sub[i] : (mopo_float)0.0;
          mopo_float noise_f1 = (noise_target == 0 || noise_target == 1) ? noise[i] : (mopo_float)0.0;
          mopo_float l = 0.0f;
          mopo_float r = 0.0f;
          if (mode == kRoutingSeries12) {
            l = osc1_l[i] + osc2_l[i] + sub_f1 + noise_f1;
            r = osc1_r[i] + osc2_r[i] + sub_f1 + noise_f1;
          } else if (mode == kRoutingParallel || mode == kRoutingSeries1Split) {
            l = osc1_l[i] + sub_f1 + noise_f1;
            r = osc1_r[i] + sub_f1 + noise_f1;
          } else { // kRoutingSeries2Split
            l = osc2_l[i] + sub_f1 + noise_f1;
            r = osc2_r[i] + sub_f1 + noise_f1;
          }
          out_l[i] = l;
          out_r[i] = r;
          MOPO_ASSERT(std::isfinite(out_l[i]));
          MOPO_ASSERT(std::isfinite(out_r[i]));
        }
      }
  };

  class Filter2InputRouter : public Processor {
    public:
      enum Inputs {
        kFilter1OutLeft,
        kFilter1OutRight,
        kFilter1Pan,
        kOsc1Left,
        kOsc1Right,
        kOsc2Left,
        kOsc2Right,
        kSub,
        kNoise,
        kRoutingMode,
        kSubTarget,
        kNoiseTarget,
        kNumInputs
      };

      Filter2InputRouter() : Processor(kNumInputs, 2) { }
      virtual ~Filter2InputRouter() { }

      virtual Processor* clone() const override {
        return new Filter2InputRouter(*this);
      }

      virtual void process() override {
        int mode = utils::iclamp(static_cast<int>(input(kRoutingMode)->at(0) + 0.5f), 0, 3);
        int sub_target = utils::iclamp(static_cast<int>(input(kSubTarget)->at(0) + 0.5f), 0, 2);
        int noise_target = utils::iclamp(static_cast<int>(input(kNoiseTarget)->at(0) + 0.5f), 0, 2);
        mopo_float pan1 = utils::clamp(input(kFilter1Pan)->at(0), (mopo_float)-1.0, (mopo_float)1.0);
        mopo_float pan1_gain_l = (mopo_float)1.0 - std::max<mopo_float>(0.0, pan1);
        mopo_float pan1_gain_r = (mopo_float)1.0 + std::min<mopo_float>(0.0, pan1);

        const mopo_float* f1_l = input(kFilter1OutLeft)->source->buffer;
        const mopo_float* f1_r = input(kFilter1OutRight)->source->buffer;
        const mopo_float* osc1_l = input(kOsc1Left)->source->buffer;
        const mopo_float* osc1_r = input(kOsc1Right)->source->buffer;
        const mopo_float* osc2_l = input(kOsc2Left)->source->buffer;
        const mopo_float* osc2_r = input(kOsc2Right)->source->buffer;
        const mopo_float* sub = input(kSub)->source->buffer;
        const mopo_float* noise = input(kNoise)->source->buffer;

        mopo_float* out_l = output(0)->buffer;
        mopo_float* out_r = output(1)->buffer;

        VECTORIZE_LOOP
        for (int i = 0; i < buffer_size_; ++i) {
          mopo_float l = 0.0f;
          mopo_float r = 0.0f;
          if (mode == kRoutingSeries12) {
            mopo_float sub_direct = (sub_target == 2) ? sub[i] : (mopo_float)0.0;
            mopo_float noise_direct = (noise_target == 2) ? noise[i] : (mopo_float)0.0;
            l = f1_l[i] * pan1_gain_l + sub_direct + noise_direct;
            r = f1_r[i] * pan1_gain_r + sub_direct + noise_direct;
          } else {
            mopo_float sub_f2 = (sub_target == 0 || sub_target == 2) ? sub[i] : (mopo_float)0.0;
            mopo_float noise_f2 = (noise_target == 0 || noise_target == 2) ? noise[i] : (mopo_float)0.0;
            if (mode == kRoutingParallel) {
              l = osc2_l[i] + sub_f2 + noise_f2;
              r = osc2_r[i] + sub_f2 + noise_f2;
            } else if (mode == kRoutingSeries1Split) {
              l = f1_l[i] * pan1_gain_l + osc2_l[i] + sub_f2 + noise_f2;
              r = f1_r[i] * pan1_gain_r + osc2_r[i] + sub_f2 + noise_f2;
            } else { // kRoutingSeries2Split
              l = f1_l[i] * pan1_gain_l + osc1_l[i] + sub_f2 + noise_f2;
              r = f1_r[i] * pan1_gain_r + osc1_l[i] + sub_f2 + noise_f2;
            }
          }
          out_l[i] = l;
          out_r[i] = r;
          MOPO_ASSERT(std::isfinite(out_l[i]));
          MOPO_ASSERT(std::isfinite(out_r[i]));
        }
      }
  };

  class FilterOutputMixer : public Processor {
    public:
      enum Inputs {
        kFilter1OutLeft,
        kFilter1OutRight,
        kFilter1Pan,
        kFilter2OutLeft,
        kFilter2OutRight,
        kFilter2Pan,
        kRoutingMode,
        kNumInputs
      };

      FilterOutputMixer() : Processor(kNumInputs, 2) { }
      virtual ~FilterOutputMixer() { }

      virtual Processor* clone() const override {
        return new FilterOutputMixer(*this);
      }

      virtual void process() override {
        int mode = utils::iclamp(static_cast<int>(input(kRoutingMode)->at(0) + 0.5f), 0, 3);
        mopo_float pan1 = utils::clamp(input(kFilter1Pan)->at(0), (mopo_float)-1.0, (mopo_float)1.0);
        mopo_float pan2 = utils::clamp(input(kFilter2Pan)->at(0), (mopo_float)-1.0, (mopo_float)1.0);
        mopo_float pan1_gain_l = (mopo_float)1.0 - std::max<mopo_float>(0.0, pan1);
        mopo_float pan1_gain_r = (mopo_float)1.0 + std::min<mopo_float>(0.0, pan1);
        mopo_float pan2_gain_l = (mopo_float)1.0 - std::max<mopo_float>(0.0, pan2);
        mopo_float pan2_gain_r = (mopo_float)1.0 + std::min<mopo_float>(0.0, pan2);

        const mopo_float* f1_l = input(kFilter1OutLeft)->source->buffer;
        const mopo_float* f1_r = input(kFilter1OutRight)->source->buffer;
        const mopo_float* f2_l = input(kFilter2OutLeft)->source->buffer;
        const mopo_float* f2_r = input(kFilter2OutRight)->source->buffer;

        mopo_float* out_l = output(0)->buffer;
        mopo_float* out_r = output(1)->buffer;

        VECTORIZE_LOOP
        for (int i = 0; i < buffer_size_; ++i) {
          if (mode == kRoutingParallel) {
            out_l[i] = f1_l[i] * pan1_gain_l + f2_l[i] * pan2_gain_l;
            out_r[i] = f1_r[i] * pan1_gain_r + f2_r[i] * pan2_gain_r;
          } else {
            out_l[i] = f2_l[i] * pan2_gain_l;
            out_r[i] = f2_r[i] * pan2_gain_r;
          }
          MOPO_ASSERT(std::isfinite(out_l[i]));
          MOPO_ASSERT(std::isfinite(out_r[i]));
        }
      }
  };

} // namespace mopo

#endif // HELM_FILTER_ROUTER_H

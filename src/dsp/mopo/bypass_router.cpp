/* Copyright 2013-2017 Matt Tytel
 *
 * mopo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mopo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mopo.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bypass_router.h"

namespace mopo {

  BypassRouter::BypassRouter(int num_inputs, int num_outputs) :
      ProcessorRouter(num_inputs, num_outputs) { }

  void BypassRouter::process() {
    MOPO_ASSERT(inputMatchesBufferSize(kAudio));

    mopo_float should_process = input(numInputs() - 1)->at(0);
    if (should_process) {
      ProcessorRouter::process();
    } else {
      if (numOutputs() == 1) {
        utils::copyBuffer(output(0)->buffer, input(kAudio)->source->buffer, buffer_size_);
      } else {
        for (int i = 0; i < numOutputs(); ++i) {
          int in_idx = (i < numInputs() - 1 && input(i)->source) ? i : 0;
          utils::copyBuffer(output(i)->buffer, input(in_idx)->source->buffer, buffer_size_);
        }
      }
    }
  }
} // namespace mopo

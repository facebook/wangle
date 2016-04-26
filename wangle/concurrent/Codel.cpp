/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/concurrent/Codel.h>

#include <algorithm>
#include <folly/portability/GFlags.h>
#include <math.h>

DEFINE_int32(codel_interval, 100,
             "Codel default interval time in ms");
DEFINE_int32(codel_target_delay, 5,
             "Target codel queueing delay in ms");

using std::chrono::nanoseconds;
using std::chrono::milliseconds;

namespace wangle {

Codel::Codel()
    : codelMinDelay_(0),
      codelIntervalTime_(std::chrono::steady_clock::now()),
      codelResetDelay_(true),
      overloaded_(false) {}

bool Codel::overloaded(std::chrono::nanoseconds delay) {
  bool ret = false;
  auto now = std::chrono::steady_clock::now();

  // Avoid another thread updating the value at the same time we are using it
  // to calculate the overloaded state
  auto minDelay = codelMinDelay_;

  if (now  > codelIntervalTime_ &&
      // testing before exchanging is more cacheline-friendly
      (!codelResetDelay_.load(std::memory_order_acquire)
       && !codelResetDelay_.exchange(true))) {
    codelIntervalTime_ = now + getInterval();

    if (minDelay > getTargetDelay()) {
      overloaded_ = true;
    } else {
      overloaded_ = false;
    }
  }
  // Care must be taken that only a single thread resets codelMinDelay_,
  // and that it happens after the interval reset above
  if (codelResetDelay_.load(std::memory_order_acquire) &&
      codelResetDelay_.exchange(false)) {
    codelMinDelay_ = delay;
    // More than one request must come in during an interval before codel
    // starts dropping requests
    return false;
  } else if(delay < codelMinDelay_) {
    codelMinDelay_ = delay;
  }

  // Here is where we apply different logic than codel proper. Instead of
  // adapting the interval until the next drop, we slough off requests with
  // queueing delay > 2*target_delay while in the overloaded regime. This
  // empirically works better for our services than the codel approach of
  // increasingly often dropping packets.
  if (overloaded_ && delay > getSloughTimeout()) {
    ret = true;
  }

  return ret;

}

int Codel::getLoad() {
  // it might be better to use the average delay instead of minDelay, but we'd
  // have to track it. aspiring bootcamper?
  return std::min<int>(100, 100 * getMinDelay() / getSloughTimeout());
}

nanoseconds Codel::getMinDelay() {
  return codelMinDelay_;
}

milliseconds Codel::getInterval() {
  return milliseconds(FLAGS_codel_interval);
}

milliseconds Codel::getTargetDelay() {
  return milliseconds(FLAGS_codel_target_delay);
}

milliseconds Codel::getSloughTimeout() {
  return getTargetDelay() * 2;
}

} // namespace wangle

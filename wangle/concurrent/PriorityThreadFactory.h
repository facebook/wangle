/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <wangle/concurrent/ThreadFactory.h>

#include <folly/MoveWrapper.h>
#include <folly/portability/SysResource.h>
#include <folly/portability/SysTime.h>

namespace wangle {

/**
 * A ThreadFactory that sets nice values for each thread.  The main
 * use case for this class is if there are multiple
 * CPUThreadPoolExecutors in a single process, or between multiple
 * processes, where some should have a higher priority than the others.
 *
 * Note that per-thread nice values are not POSIX standard, but both
 * pthreads and linux support per-thread nice.  The default linux
 * scheduler uses these values to do smart thread prioritization.
 * sched_priority function calls only affect real-time schedulers.
 */
class PriorityThreadFactory : public ThreadFactory {
 public:
  explicit PriorityThreadFactory(std::shared_ptr<ThreadFactory> factory,
                                int priority)
    : factory_(std::move(factory))
    , priority_(priority) {}

  std::thread newThread(folly::Func&& func) override {
    folly::MoveWrapper<folly::Func> movedFunc(std::move(func));
    int priority = priority_;
    return factory_->newThread([priority,movedFunc] () {
      if (setpriority(PRIO_PROCESS, 0, priority) != 0) {
        LOG(ERROR) << "setpriority failed (are you root?) with error " <<
          errno, strerror(errno);
      }
      (*movedFunc)();
    });
  }

 private:
  std::shared_ptr<ThreadFactory> factory_;
  int priority_;
};

} // wangle

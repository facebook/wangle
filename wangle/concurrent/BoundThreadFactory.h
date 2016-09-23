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

namespace wangle {

/**
 * A ThreadFactory that sets binds each thread to a specific CPU core.  
 * The main use case for this class is NUMA-aware computing.
 */
class BoundThreadFactory : public ThreadFactory {
 public:
  explicit BoundThreadFactory(std::shared_ptr<ThreadFactory> factory,
                             int coreId)
    : factory_(std::move(factory))
    , coreId_(coreId) {}

  std::thread newThread(folly::Func&& func) override {
    int coreId = coreId_;
    return factory_->newThread([ coreId, func = std::move(func) ]() mutable {
      cpu_set_t cpuSet;
      CPU_ZERO(&cpuSet);
      CPU_SET(coreId, &cpuSet);
      int error = pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet);
      if (error != 0) {
        LOG(ERROR) << "set cpu affinity failed for core=" << coreId 
          << " with error " << error, strerror(error);
      } 
      func();
    });
  }

 private:
  std::shared_ptr<ThreadFactory> factory_;
  int coreId_;
};

} // wangle

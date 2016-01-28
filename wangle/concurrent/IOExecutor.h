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

#include <atomic>
#include <folly/Executor.h>

namespace folly {
class EventBase;
}

namespace wangle {

// An IOExecutor is an executor that operates on at least one EventBase.  One of
// these EventBases should be accessible via getEventBase(). The event base
// returned by a call to getEventBase() is implementation dependent.
//
// Note that IOExecutors don't necessarily loop on the base themselves - for
// instance, EventBase itself is an IOExecutor but doesn't drive itself.
//
// Implementations of IOExecutor are eligible to become the global IO executor,
// returned on every call to getIOExecutor(), via setIOExecutor().
// These functions are declared in GlobalExecutor.h
//
// If getIOExecutor is called and none has been set, a default global
// IOThreadPoolExecutor will be created and returned.
class IOExecutor : public virtual folly::Executor {
 public:
  virtual ~IOExecutor() = default;
  virtual folly::EventBase* getEventBase() = 0;
};

} // namespace wangle

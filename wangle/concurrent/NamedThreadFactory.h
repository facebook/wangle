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
#include <string>
#include <thread>

#include <wangle/concurrent/ThreadFactory.h>
#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/ThreadName.h>

namespace wangle {

class NamedThreadFactory : public ThreadFactory {
 public:
  explicit NamedThreadFactory(folly::StringPiece prefix)
    : prefix_(prefix.str()), suffix_(0) {}

  std::thread newThread(folly::Func&& func) override {
    auto thread = std::thread(std::move(func));
    folly::setThreadName(
        thread.native_handle(),
        folly::to<std::string>(prefix_, suffix_++));
    return thread;
  }

  void setNamePrefix(folly::StringPiece prefix) {
    prefix_ = prefix.str();
  }

  std::string getNamePrefix() {
    return prefix_;
  }

 private:
  std::string prefix_;
  std::atomic<uint64_t> suffix_;
};

} // namespace wangle

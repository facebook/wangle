/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <queue>

#include <folly/LifoSem.h>
#include <folly/Synchronized.h>
#include <wangle/concurrent/BlockingQueue.h>

namespace wangle {

// Warning: this is effectively just a std::deque wrapped in a single mutex
// We are aiming to add a more performant concurrent unbounded queue in the
// future, but this class is available if you must have an unbounded queue
// and can tolerate any contention.
template <class T>
class UnboundedBlockingQueue : public BlockingQueue<T> {
 public:
  virtual ~UnboundedBlockingQueue() {}

  void add(T item) override {
    queue_.wlock()->push(std::move(item));
    sem_.post();
  }

  T take() override {
    while (true) {
      {
        auto ulockedQueue = queue_.ulock();
        if (!ulockedQueue->empty()) {
          auto wlockedQueue = ulockedQueue.moveFromUpgradeToWrite();
          T item = std::move(wlockedQueue->front());
          wlockedQueue->pop();
          return item;
        }
      }
      sem_.wait();
    }
  }

  size_t size() override {
    return queue_.rlock()->size();
  }

 private:
  folly::LifoSem sem_;
  folly::Synchronized<std::queue<T>> queue_;
};

} // namespace wangle

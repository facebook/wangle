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
#include <wangle/concurrent/BlockingQueue.h>
#include <folly/MPMCQueue.h>

namespace wangle {

template <class T>
class BlockingMPMCQueue : public BlockingQueue<T> {
 public:
  explicit BlockingMPMCQueue(size_t max_capacity) : queue_(max_capacity) {}

  void add(T item) override {
    queue_.blockingWrite(std::move(item));
  }

  T take() override {
    T item;
    queue_.blockingRead(item);
    return item;
  }

  size_t capacity() {
    return queue_.capacity();
  }

  size_t size() override {
    return queue_.size();
  }

 private:
  folly::MPMCQueue<T> queue_;
};

} // namespace wangle

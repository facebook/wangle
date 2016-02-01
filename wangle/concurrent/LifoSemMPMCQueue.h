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
#include <folly/LifoSem.h>
#include <folly/MPMCQueue.h>

namespace wangle {

template <class T, QueueBehaviorIfFull kBehavior = QueueBehaviorIfFull::THROW>
class LifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  explicit LifoSemMPMCQueue(size_t max_capacity) : queue_(max_capacity) {}

  void add(T item) override {
    switch (kBehavior) { // static
    case QueueBehaviorIfFull::THROW:
      if (!queue_.write(std::move(item))) {
        throw std::runtime_error("LifoSemMPMCQueue full, can't add item");
      }
      break;
    case QueueBehaviorIfFull::BLOCK:
      queue_.blockingWrite(std::move(item));
      break;
    }
    sem_.post();
  }

  T take() override {
    T item;
    while (!queue_.read(item)) {
      sem_.wait();
    }
    return item;
  }

  size_t capacity() {
    return queue_.capacity();
  }

  size_t size() override {
    return queue_.size();
  }

 private:
  folly::LifoSem sem_;
  folly::MPMCQueue<T> queue_;
};

} // namespace wangle

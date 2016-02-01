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
class PriorityLifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  explicit PriorityLifoSemMPMCQueue(uint8_t numPriorities, size_t capacity) {
    queues_.reserve(numPriorities);
    for (int8_t i = 0; i < numPriorities; i++) {
      queues_.emplace_back(capacity);
    }
  }

  uint8_t getNumPriorities() override {
    return queues_.size();
  }

  // Add at medium priority by default
  void add(T item) override {
    addWithPriority(std::move(item), folly::Executor::MID_PRI);
  }

  void addWithPriority(T item, int8_t priority) override {
    int mid = getNumPriorities() / 2;
    size_t queue = priority < 0 ?
                   std::max(0, mid + priority) :
                   std::min(getNumPriorities() - 1, mid + priority);
    CHECK_LT(queue, queues_.size());
    switch (kBehavior) { // static
    case QueueBehaviorIfFull::THROW:
      if (!queues_[queue].write(std::move(item))) {
        throw std::runtime_error("LifoSemMPMCQueue full, can't add item");
      }
      break;
    case QueueBehaviorIfFull::BLOCK:
      queues_[queue].blockingWrite(std::move(item));
      break;
    }
    sem_.post();
  }

  T take() override {
    T item;
    while (true) {
      for (auto it = queues_.rbegin(); it != queues_.rend(); it++) {
        if (it->read(item)) {
          return item;
        }
      }
      sem_.wait();
    }
  }

  size_t size() override {
    size_t size = 0;
    for (auto& q : queues_) {
      size += q.size();
    }
    return size;
  }

 private:
  folly::LifoSem sem_;
  std::vector<folly::MPMCQueue<T>> queues_;
};

} // namespace wangle

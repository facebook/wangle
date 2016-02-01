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

#include <glog/logging.h>

namespace wangle {

// Some queue implementations (for example, LifoSemMPMCQueue or
// PriorityLifoSemMPMCQueue) support both blocking (BLOCK) and
// non-blocking (THROW) behaviors.
enum class QueueBehaviorIfFull { THROW, BLOCK };

template <class T>
class BlockingQueue {
 public:
  virtual ~BlockingQueue() = default;
  virtual void add(T item) = 0;
  virtual void addWithPriority(T item, int8_t priority) {
    add(std::move(item));
  }
  virtual uint8_t getNumPriorities() {
    return 1;
  }
  virtual T take() = 0;
  virtual size_t size() = 0;
};

} // namespace wangle

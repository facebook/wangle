/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "SerialExecutor.h"

#include <mutex>
#include <queue>

#include <glog/logging.h>

#include <folly/ExceptionString.h>

namespace wangle {

class SerialExecutor::TaskQueueImpl {
 public:
  void add(folly::Func&& func) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(func));
  }

  void run() {
    std::unique_lock<std::mutex> lock(mutex_);

    ++scheduled_;

    if (scheduled_ > 1) {
      return;
    }

    do {
      DCHECK(!queue_.empty());
      folly::Func func = std::move(queue_.front());
      queue_.pop();
      lock.unlock();

      try {
        func();
      } catch (std::exception const& ex) {
        LOG(ERROR) << "SerialExecutor: func threw unhandled exception "
                   << folly::exceptionStr(ex);
      } catch (...) {
        LOG(ERROR) << "SerialExecutor: func threw unhandled non-exception "
                      "object";
      }

      // Destroy the function (and the data it captures) before we acquire the
      // lock again.
      func = {};

      lock.lock();
      --scheduled_;
    } while (scheduled_);
  }

 private:
  std::mutex mutex_;
  std::size_t scheduled_{0};
  std::queue<folly::Func> queue_;
};

SerialExecutor::SerialExecutor(std::shared_ptr<folly::Executor> parent)
    : parent_(std::move(parent)),
      taskQueueImpl_(std::make_shared<TaskQueueImpl>()) {}

void SerialExecutor::add(folly::Func func) {
  taskQueueImpl_->add(std::move(func));
  parent_->add([impl = taskQueueImpl_] { impl->run(); });
}

void SerialExecutor::addWithPriority(folly::Func func, int8_t priority) {
  taskQueueImpl_->add(std::move(func));
  parent_->addWithPriority([impl = taskQueueImpl_] { impl->run(); }, priority);
}

} // namespace wangle

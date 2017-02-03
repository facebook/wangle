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

#include <memory>

#include <folly/Executor.h>
#include <wangle/concurrent/GlobalExecutor.h>

namespace wangle {

/**
 * @class SerialExecutor
 *
 * @brief Executor that guarantees serial non-concurrent execution of added
 *     tasks
 *
 * SerialExecutor is similar to boost asio's strand concept. A SerialExecutor
 * has a parent executor which is given at construction time (defaults to
 * wangle's global CPUExecutor). Tasks added to SerialExecutor are executed
 * in the parent executor, however strictly non-concurrently and in the order
 * they were added.
 *
 * SerialExecutor tries to schedule its tasks fairly. Every task submitted to
 * it results in one task submitted to the parent executor. Whenever the parent
 * executor executes one of those, one of the tasks submitted to SerialExecutor
 * is marked for execution, which means it will either be executed at once,
 * or if a task is currently being executed already, after that.
 *
 * The SerialExecutor may be deleted at any time. All tasks that have been
 * submitted will still be executed with the same guarantees, as long as the
 * parent executor is executing tasks.
 */

class SerialExecutor : public folly::Executor {
 public:
  ~SerialExecutor() override = default;
  SerialExecutor(SerialExecutor const&) = delete;
  SerialExecutor& operator=(SerialExecutor const&) = delete;
  SerialExecutor(SerialExecutor&&) = default;
  SerialExecutor& operator=(SerialExecutor&&) = default;

  explicit SerialExecutor(
      std::shared_ptr<folly::Executor> parent = wangle::getCPUExecutor());

  /**
   * Add one task for execution in the parent executor
   */
  void add(folly::Func func) override;

  /**
   * Add one task for execution in the parent executor, and use the given
   * priority for one task submission to parent executor.
   *
   * Since in-order execution of tasks submitted to SerialExecutor is
   * guaranteed, the priority given here does not necessarily reflect the
   * execution priority of the task submitted with this call to
   * `addWithPriority`. The given priority is passed on to the parent executor
   * for the execution of one of the SerialExecutor's tasks.
   */
  void addWithPriority(folly::Func func, int8_t priority) override;
  uint8_t getNumPriorities() const override {
    return parent_->getNumPriorities();
  }

 private:
  class TaskQueueImpl;

  std::shared_ptr<folly::Executor> parent_;
  std::shared_ptr<TaskQueueImpl> taskQueueImpl_;
};

} // namespace wangle

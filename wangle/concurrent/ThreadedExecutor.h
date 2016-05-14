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

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <folly/Executor.h>
#include <wangle/concurrent/ThreadFactory.h>

namespace wangle {

/***
 *  ThreadedExecutor
 *
 *  An executor for blocking tasks.
 *
 *  This executor runs each task in its own thread. It works well for tasks
 *  which mostly sleep, but works poorly for tasks which mostly compute.
 *
 *  For each task given to the executor with `add`, the executor spawns a new
 *  thread for that task, runs the task in that thread, and joins the thread
 *  after the task has completed.
 *
 *  Spawning and joining task threads are done in the executor's internal
 *  control thread. Calls to `add` put the tasks to be run into a queue, where
 *  the control thread will find them.
 *
 *  There is currently no limitation on, or throttling of, concurrency.
 *
 *  This executor is not currently optimized for performance. For example, it
 *  makes no attempt to re-use task threads. Rather, it exists primarily to
 *  offload sleep-heavy tasks from the CPU executor, where they might otherwise
 *  be run.
 */
class ThreadedExecutor : public virtual folly::Executor {
 public:
  explicit ThreadedExecutor(
      std::shared_ptr<ThreadFactory> threadFactory = newDefaultThreadFactory());
  ~ThreadedExecutor();

  ThreadedExecutor(ThreadedExecutor const&) = delete;
  ThreadedExecutor(ThreadedExecutor&&) = delete;

  ThreadedExecutor& operator=(ThreadedExecutor const&) = delete;
  ThreadedExecutor& operator=(ThreadedExecutor&&) = delete;

  void add(folly::Func func) override;

 private:
  static std::shared_ptr<ThreadFactory> newDefaultThreadFactory();

  void notify();
  void control();
  void controlWait();
  bool controlPerformAll();
  void controlJoinFinishedThreads();
  void controlLaunchEnqueuedTasks();

  void work(folly::Func& func);

  std::shared_ptr<ThreadFactory> threadFactory_;

  std::atomic<bool> stopping_{false};

  std::mutex controlm_;
  std::condition_variable controlc_;
  bool controls_ = false;
  std::thread controlt_;

  std::mutex enqueuedm_;
  std::deque<folly::Func> enqueued_;

  //  Accessed only by the control thread, so no synchronization.
  std::map<std::thread::id, std::thread> running_;

  std::mutex finishedm_;
  std::deque<std::thread::id> finished_;
};

}

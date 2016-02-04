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

#include <wangle/concurrent/ThreadPoolExecutor.h>

namespace wangle {

/**
 * A Thread pool for CPU bound tasks.
 *
 * @note A single queue backed by folly/LifoSem and folly/MPMC queue.
 * Because of this contention can be quite high,
 * since all the worker threads and all the producer threads hit
 * the same queue. MPMC queue excels in this situation but dictates a max queue
 * size
 *
 * @note LifoSem wakes up threads in Lifo order - i.e. there are only few
 * threads as necessary running, and we always try to reuse the same few threads
 * for better cache locality.
 * Inactive threads have their stack madvised away. This works quite well in
 * combination with Lifosem - it almost doesn't matter if more threads than are
 * necessary are specified at startup.
 *
 * @note stop() will finish all outstanding tasks at exit
 *
 * @note Supports priorities - priorities are implemented as multiple queues -
 * each worker thread checks the highest priority queue first. Threads
 * themselves don't have priorities set, so a series of long running low
 * priority tasks could still hog all the threads. (at last check pthreads
 * thread priorities didn't work very well)
 */
class CPUThreadPoolExecutor : public ThreadPoolExecutor {
 public:
  struct CPUTask;

  CPUThreadPoolExecutor(
      size_t numThreads,
      std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  explicit CPUThreadPoolExecutor(size_t numThreads);
CPUThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory);

  CPUThreadPoolExecutor(
      size_t numThreads,
      int8_t numPriorities,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  CPUThreadPoolExecutor(
      size_t numThreads,
      int8_t numPriorities,
      size_t maxQueueSize,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  ~CPUThreadPoolExecutor();

  void add(folly::Func func) override;
  void add(
      folly::Func func,
      std::chrono::milliseconds expiration,
      folly::Func expireCallback = nullptr) override;

  void addWithPriority(folly::Func func, int8_t priority) override;
  void add(
      folly::Func func,
      int8_t priority,
      std::chrono::milliseconds expiration,
      folly::Func expireCallback = nullptr);

  uint8_t getNumPriorities() const override;

  struct CPUTask : public ThreadPoolExecutor::Task {
    // Must be noexcept move constructible so it can be used in MPMCQueue
    explicit CPUTask(
        folly::Func&& f,
        std::chrono::milliseconds expiration,
        folly::Func&& expireCallback)
      : Task(std::move(f), expiration, std::move(expireCallback)),
        poison(false) {}
    CPUTask()
      : Task(nullptr, std::chrono::milliseconds(0), nullptr),
        poison(true) {}
    CPUTask(CPUTask&& o) noexcept : Task(std::move(o)), poison(o.poison) {}
    CPUTask(const CPUTask&) = default;
    CPUTask& operator=(const CPUTask&) = default;
    bool poison;
  };

  static const size_t kDefaultMaxQueueSize;

 protected:
  BlockingQueue<CPUTask>* getTaskQueue();

 private:
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  uint64_t getPendingTaskCount() override;

  std::unique_ptr<BlockingQueue<CPUTask>> taskQueue_;
  std::atomic<ssize_t> threadsToStop_{0};
};

} // namespace wangle

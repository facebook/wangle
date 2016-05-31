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

#include <folly/io/async/EventBaseManager.h>
#include <wangle/concurrent/IOExecutor.h>
#include <wangle/concurrent/ThreadPoolExecutor.h>

namespace wangle {

/**
 * A Thread Pool for IO bound tasks
 *
 * @note Uses event_fd for notification, and waking an epoll loop.
 * There is one queue (NotificationQueue specifically) per thread/epoll.
 * If the thread is already running and not waiting on epoll,
 * we don't make any additional syscalls to wake up the loop,
 * just put the new task in the queue.
 * If any thread has been waiting for more than a few seconds,
 * its stack is madvised away. Currently however tasks are scheduled round
 * robin on the queues, so unless there is no work going on,
 * this isn't very effective.
 * Since there is one queue per thread, there is hardly any contention
 * on the queues - so a simple spinlock around an std::deque is used for
 * the tasks. There is no max queue size.
 * By default, there is one thread per core - it usually doesn't make sense to
 * have more IO threads than this, assuming they don't block.
 *
 * @note ::getEventBase() will return an EventBase you can schedule IO work on
 * directly, chosen round-robin.
 *
 * @note N.B. For this thread pool, stop() behaves like join() because
 * outstanding tasks belong to the event base and will be executed upon its
 * destruction.
 */
class IOThreadPoolExecutor : public ThreadPoolExecutor, public IOExecutor {
 public:
  explicit IOThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("IOThreadPool"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get(),
      bool waitForAll = false);

  ~IOThreadPoolExecutor();

  void add(folly::Func func) override;
  void add(
      folly::Func func,
      std::chrono::milliseconds expiration,
      folly::Func expireCallback = nullptr) override;

  folly::EventBase* getEventBase() override;

  static folly::EventBase* getEventBase(ThreadPoolExecutor::ThreadHandle*);

  folly::EventBaseManager* getEventBaseManager();

 private:
  struct FOLLY_ALIGN_TO_AVOID_FALSE_SHARING IOThread : public Thread {
    IOThread(IOThreadPoolExecutor* pool)
      : Thread(pool),
        shouldRun(true),
        pendingTasks(0) {};
    std::atomic<bool> shouldRun;
    std::atomic<size_t> pendingTasks;
    folly::EventBase* eventBase;
  };

  ThreadPtr makeThread() override;
  std::shared_ptr<IOThread> pickThread();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  uint64_t getPendingTaskCount() override;

  size_t nextThread_;
  folly::ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  folly::EventBaseManager* eventBaseManager_;
};

} // namespace wangle

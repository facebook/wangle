/*
 *  Copyright (c) 2015, Facebook, Inc.
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

// N.B. For this thread pool, stop() behaves like join() because outstanding
// tasks belong to the event base and will be executed upon its destruction.
class IOThreadPoolExecutor : public ThreadPoolExecutor, public IOExecutor {
 public:
  explicit IOThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("IOThreadPool"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get());

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

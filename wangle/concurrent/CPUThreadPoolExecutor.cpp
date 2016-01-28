/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/concurrent/CPUThreadPoolExecutor.h>
#include <wangle/concurrent/PriorityLifoSemMPMCQueue.h>

using folly::Func;

namespace wangle {

const size_t CPUThreadPoolExecutor::kDefaultMaxQueueSize = 1 << 14;

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
    std::shared_ptr<ThreadFactory> threadFactory)
    : ThreadPoolExecutor(numThreads, std::move(threadFactory)),
      taskQueue_(std::move(taskQueue)) {
  addThreads(numThreads);
  CHECK(threadList_.get().size() == numThreads);
}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          folly::make_unique<LifoSemMPMCQueue<CPUTask>>(
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(size_t numThreads)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_shared<NamedThreadFactory>("CPUThreadPool")) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    int8_t numPriorities,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          folly::make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
              numPriorities,
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    int8_t numPriorities,
    size_t maxQueueSize,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          folly::make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
              numPriorities,
              maxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::~CPUThreadPoolExecutor() {
  stop();
  CHECK(threadsToStop_ == 0);
}

void CPUThreadPoolExecutor::add(Func func) {
  add(std::move(func), std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::add(
    Func func,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  // TODO handle enqueue failure, here and in other add() callsites
  taskQueue_->add(
      CPUTask(std::move(func), expiration, std::move(expireCallback)));
}

void CPUThreadPoolExecutor::addWithPriority(Func func, int8_t priority) {
  add(std::move(func), priority, std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::add(
    Func func,
    int8_t priority,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  CHECK(getNumPriorities() > 0);
  taskQueue_->addWithPriority(
      CPUTask(std::move(func), expiration, std::move(expireCallback)),
      priority);
}

uint8_t CPUThreadPoolExecutor::getNumPriorities() const {
  return taskQueue_->getNumPriorities();
}

BlockingQueue<CPUThreadPoolExecutor::CPUTask>*
CPUThreadPoolExecutor::getTaskQueue() {
  return taskQueue_.get();
}

void CPUThreadPoolExecutor::threadRun(std::shared_ptr<Thread> thread) {
  thread->startupBaton.post();
  while (1) {
    auto task = taskQueue_->take();
    if (UNLIKELY(task.poison)) {
      CHECK(threadsToStop_-- > 0);
      for (auto& o : observers_) {
        o->threadStopped(thread.get());
      }

      stoppedThreads_.add(thread);
      return;
    } else {
      runTask(thread, std::move(task));
    }

    if (UNLIKELY(threadsToStop_ > 0 && !isJoin_)) {
      if (--threadsToStop_ >= 0) {
        stoppedThreads_.add(thread);
        return;
      } else {
        threadsToStop_++;
      }
    }
  }
}

void CPUThreadPoolExecutor::stopThreads(size_t n) {
  CHECK(stoppedThreads_.size() == 0);
  threadsToStop_ = n;
  for (size_t i = 0; i < n; i++) {
    taskQueue_->addWithPriority(CPUTask(), Executor::LO_PRI);
  }
}

uint64_t CPUThreadPoolExecutor::getPendingTaskCount() {
  return taskQueue_->size();
}

} // namespace wangle

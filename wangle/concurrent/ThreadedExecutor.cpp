/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/concurrent/ThreadedExecutor.h>

#include <chrono>

#include <glog/logging.h>

#include <folly/ThreadName.h>
#include <wangle/concurrent/NamedThreadFactory.h>

namespace wangle {

template <typename F>
static auto with_unique_lock(std::mutex& m, F&& f) -> decltype(f()) {
  std::unique_lock<std::mutex> lock(m);
  return f();
}

ThreadedExecutor::ThreadedExecutor(
    std::shared_ptr<ThreadFactory> threadFactory)
    : threadFactory_(std::move(threadFactory)) {
  controlt_ = std::thread([this] { control(); });
}

ThreadedExecutor::~ThreadedExecutor() {
  stopping_.store(std::memory_order_release);
  notify();
  controlt_.join();
  CHECK(running_.empty());
  CHECK(finished_.empty());
}

void ThreadedExecutor::add(folly::Func func) {
  CHECK(!stopping_.load(std::memory_order_acquire));
  with_unique_lock(enqueuedm_, [&] { enqueued_.push_back(std::move(func)); });
  notify();
}

std::shared_ptr<ThreadFactory> ThreadedExecutor::newDefaultThreadFactory() {
  return std::make_shared<NamedThreadFactory>("Threaded");
}

void ThreadedExecutor::notify() {
  with_unique_lock(controlm_, [&] { controls_ = true; });
  controlc_.notify_one();
}

void ThreadedExecutor::control() {
  folly::setThreadName("ThreadedCtrl");
  auto looping = true;
  while (looping) {
    controlWait();
    looping = controlPerformAll();
  }
}

void ThreadedExecutor::controlWait() {
  constexpr auto kMaxWait = std::chrono::seconds(10);
  std::unique_lock<std::mutex> lock(controlm_);
  controlc_.wait_for(lock, kMaxWait, [&] { return controls_; });
  controls_ = false;
}

void ThreadedExecutor::work(folly::Func& func) {
  func();
  auto id = std::this_thread::get_id();
  with_unique_lock(finishedm_, [&] { finished_.push_back(id); });
  notify();
}

void ThreadedExecutor::controlJoinFinishedThreads() {
  std::deque<std::thread::id> finishedt;
  with_unique_lock(finishedm_, [&] { std::swap(finishedt, finished_); });
  for (auto id : finishedt) {
    running_[id].join();
    running_.erase(id);
  }
}

void ThreadedExecutor::controlLaunchEnqueuedTasks() {
  std::deque<folly::Func> enqueuedt;
  with_unique_lock(enqueuedm_, [&] { std::swap(enqueuedt, enqueued_); });
  for (auto& f : enqueuedt) {
    auto th = threadFactory_->newThread([this, f = std::move(f)]() mutable {
      work(f);
    });
    auto id = th.get_id();
    running_[id] = std::move(th);
  }
}

bool ThreadedExecutor::controlPerformAll() {
  auto stopping = stopping_.load(std::memory_order_acquire);
  controlJoinFinishedThreads();
  controlLaunchEnqueuedTasks();
  return !stopping || !running_.empty();
}

}

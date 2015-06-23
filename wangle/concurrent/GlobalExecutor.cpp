/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/Singleton.h>
#include <wangle/concurrent/IOExecutor.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <folly/futures/InlineExecutor.h>

using namespace folly;
using namespace folly::wangle;

namespace {

// lock protecting global CPU executor
struct CPUExecutorLock {};
Singleton<RWSpinLock, CPUExecutorLock> globalCPUExecutorLock;
// global CPU executor
Singleton<std::weak_ptr<Executor>> globalCPUExecutor;
// default global CPU executor is an InlineExecutor
Singleton<std::shared_ptr<InlineExecutor>> globalInlineExecutor(
    []{
      return new std::shared_ptr<InlineExecutor>(
          std::make_shared<InlineExecutor>());
    });

// lock protecting global IO executor
struct IOExecutorLock {};
Singleton<RWSpinLock, IOExecutorLock> globalIOExecutorLock;
// global IO executor
Singleton<std::weak_ptr<IOExecutor>> globalIOExecutor;
// default global IO executor is an IOThreadPoolExecutor
Singleton<std::shared_ptr<IOThreadPoolExecutor>> globalIOThreadPool(
    []{
      return new std::shared_ptr<IOThreadPoolExecutor>(
          std::make_shared<IOThreadPoolExecutor>(
              sysconf(_SC_NPROCESSORS_ONLN),
              std::make_shared<NamedThreadFactory>("GlobalIOThreadPool")));
    });

}

namespace folly { namespace wangle {

template <class Exe, class DefaultExe, class LockTag>
std::shared_ptr<Exe> getExecutor(
    Singleton<std::weak_ptr<Exe>>& sExecutor,
    Singleton<std::shared_ptr<DefaultExe>>& sDefaultExecutor,
    Singleton<RWSpinLock, LockTag>& sExecutorLock) {
  std::shared_ptr<Exe> executor;
  auto singleton = sExecutor.get();
  auto lock = sExecutorLock.get();

  {
    RWSpinLock::ReadHolder guard(lock);
    if ((executor = sExecutor->lock())) {
      return executor;
    }
  }


  RWSpinLock::WriteHolder guard(lock);
  executor = singleton->lock();
  if (!executor) {
    executor = *sDefaultExecutor.get();
    *singleton = executor;
  }
  return executor;
}

template <class Exe, class LockTag>
void setExecutor(
    std::shared_ptr<Exe> executor,
    Singleton<std::weak_ptr<Exe>>& sExecutor,
    Singleton<RWSpinLock, LockTag>& sExecutorLock) {
  RWSpinLock::WriteHolder guard(sExecutorLock.get());
  *sExecutor.get() = std::move(executor);
}

std::shared_ptr<Executor> getCPUExecutor() {
  return getExecutor(
      globalCPUExecutor,
      globalInlineExecutor,
      globalCPUExecutorLock);
}

void setCPUExecutor(std::shared_ptr<Executor> executor) {
  setExecutor(
      std::move(executor),
      globalCPUExecutor,
      globalCPUExecutorLock);
}

std::shared_ptr<IOExecutor> getIOExecutor() {
  return getExecutor(
      globalIOExecutor,
      globalIOThreadPool,
      globalIOExecutorLock);
}

void setIOExecutor(std::shared_ptr<IOExecutor> executor) {
  setExecutor(
      std::move(executor),
      globalIOExecutor,
      globalIOExecutorLock);
}

}} // folly::wangle

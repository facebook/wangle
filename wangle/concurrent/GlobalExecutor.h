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

#include <memory>

#include <folly/Executor.h>
#include <wangle/concurrent/IOExecutor.h>

namespace wangle {

// Retrieve the global Executor. If there is none, a default InlineExecutor
// will be constructed and returned. This is named CPUExecutor to distinguish
// it from IOExecutor below and to hint that it's intended for CPU-bound tasks.
std::shared_ptr<folly::Executor> getCPUExecutor();

// Set an Executor to be the global Executor which will be returned by
// subsequent calls to getCPUExecutor(). Takes a non-owning (weak) reference.
void setCPUExecutor(std::shared_ptr<folly::Executor> executor);

// Retrieve the global IOExecutor. If there is none, a default
// IOThreadPoolExecutor will be constructed and returned.
//
// IOExecutors differ from Executors in that they drive and provide access to
// one or more EventBases.
std::shared_ptr<IOExecutor> getIOExecutor();

// Retrieve an event base from the global IOExecutor
folly::EventBase* getEventBase();

// Set an IOExecutor to be the global IOExecutor which will be returned by
// subsequent calls to getIOExecutor(). Takes a non-owning (weak) reference.
void setIOExecutor(std::shared_ptr<IOExecutor> executor);

} // namespace wangle

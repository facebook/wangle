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

#include <folly/ExceptionWrapper.h>
#include <folly/Executor.h>

namespace wangle {

typedef folly::exception_wrapper Error;
// The Executor is basically an rx Scheduler (by design). So just
// alias it.
typedef std::shared_ptr<folly::Executor> SchedulerPtr;

template <class T, size_t InlineObservers = 3> class Observable;
template <class T> struct Observer;
template <class T> struct Subject;

template <class T> using ObservablePtr = std::shared_ptr<Observable<T>>;
template <class T> using ObserverPtr = std::shared_ptr<Observer<T>>;
template <class T> using SubjectPtr = std::shared_ptr<Subject<T>>;

} // namespace wangle

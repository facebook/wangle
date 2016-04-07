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
#include <folly/MoveWrapper.h>
#include <folly/futures/Future.h>

namespace wangle {

template <typename ExecutorImpl>
class FutureExecutor : public ExecutorImpl {
 public:
  template <typename... Args>
  explicit FutureExecutor(Args&&... args)
    : ExecutorImpl(std::forward<Args>(args)...) {}

  /*
   * Given a function func that returns a Future<T>, adds that function to the
   * contained Executor and returns a Future<T> which will be fulfilled with
   * func's result once it has been executed.
   *
   * For example: auto f = futureExecutor.addFuture([](){
   *                return doAsyncWorkAndReturnAFuture();
   *              });
   */
  template <typename F>
  typename std::enable_if<folly::isFuture<typename std::result_of<F()>::type>::value,
                          typename std::result_of<F()>::type>::type
  addFuture(F func) {
    typedef typename std::result_of<F()>::type::value_type T;
    folly::Promise<T> promise;
    auto future = promise.getFuture();
    auto movePromise = folly::makeMoveWrapper(std::move(promise));
    auto moveFunc = folly::makeMoveWrapper(std::move(func));
    ExecutorImpl::add([movePromise, moveFunc] () mutable {
      (*moveFunc)().then([movePromise] (folly::Try<T>&& t) mutable {
        movePromise->setTry(std::move(t));
      });
    });
    return future;
  }

  /*
   * Similar to addFuture above, but takes a func that returns some non-Future
   * type T.
   *
   * For example: auto f = futureExecutor.addFuture([]() {
   *                return 42;
   *              });
   */
  template <typename F>
  typename std::enable_if<!folly::isFuture<typename std::result_of<F()>::type>::value,
                          folly::Future<typename folly::Unit::Lift<typename std::result_of<F()>::type>::type>>::type
  addFuture(F func) {
    using T = typename folly::Unit::Lift<typename std::result_of<F()>::type>::type;
    folly::Promise<T> promise;
    auto future = promise.getFuture();
    auto movePromise = folly::makeMoveWrapper(std::move(promise));
    auto moveFunc = folly::makeMoveWrapper(std::move(func));
    ExecutorImpl::add([movePromise, moveFunc] () mutable {
      movePromise->setWith(std::move(*moveFunc));
    });
    return future;
  }
};

} // namespace wangle

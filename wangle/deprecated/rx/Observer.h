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

#include <wangle/deprecated/rx/types.h> // must come first
#include <functional>
#include <memory>
#include <stdexcept>
#include <folly/Memory.h>

namespace wangle {

template <class T> struct FunctionObserver;

/// Observer interface. You can subclass it, or you can just use create()
/// to use std::functions.
template <class T>
struct Observer {
  // These are what it means to be an Observer.
  virtual void onNext(const T&) = 0;
  virtual void onError(Error) = 0;
  virtual void onCompleted() = 0;

  virtual ~Observer() = default;

  /// Create an Observer with std::function callbacks. Handy to make ad-hoc
  /// Observers with lambdas.
  ///
  /// Templated for maximum perfect forwarding flexibility, but ultimately
  /// whatever you pass in has to implicitly become a std::function for the
  /// same signature as onNext(), onError(), and onCompleted() respectively.
  /// (see the FunctionObserver typedefs)
  template <class N, class E, class C>
  static std::unique_ptr<Observer> create(
    N&& onNextFn, E&& onErrorFn, C&& onCompletedFn)
  {
    return folly::make_unique<FunctionObserver<T>>(
      std::forward<N>(onNextFn),
      std::forward<E>(onErrorFn),
      std::forward<C>(onCompletedFn));
  }

  /// Create an Observer with only onNext and onError callbacks.
  /// onCompleted will just be a no-op.
  template <class N, class E>
  static std::unique_ptr<Observer> create(N&& onNextFn, E&& onErrorFn) {
    return folly::make_unique<FunctionObserver<T>>(
      std::forward<N>(onNextFn),
      std::forward<E>(onErrorFn),
      nullptr);
  }

  /// Create an Observer with only an onNext callback.
  /// onError and onCompleted will just be no-ops.
  template <class N>
  static std::unique_ptr<Observer> create(N&& onNextFn) {
    return folly::make_unique<FunctionObserver<T>>(
      std::forward<N>(onNextFn),
      nullptr,
      nullptr);
  }
};

/// An observer that uses std::function callbacks. You don't really want to
/// make one of these directly - instead use the Observer::create() methods.
template <class T>
struct FunctionObserver : public Observer<T> {
  typedef std::function<void(const T&)> OnNext;
  typedef std::function<void(Error)> OnError;
  typedef std::function<void()> OnCompleted;

  /// We don't need any fancy overloads of this constructor because that's
  /// what Observer::create() is for.
  template <class N = OnNext, class E = OnError, class C = OnCompleted>
  FunctionObserver(N&& n, E&& e, C&& c)
    : onNext_(std::forward<N>(n)),
      onError_(std::forward<E>(e)),
      onCompleted_(std::forward<C>(c))
  {}

  void onNext(const T& val) override {
    if (onNext_) onNext_(val);
  }

  void onError(Error e) override {
    if (onError_) onError_(e);
  }

  void onCompleted() override {
    if (onCompleted_) onCompleted_();
  }

 protected:
  OnNext onNext_;
  OnError onError_;
  OnCompleted onCompleted_;
};

} // namespace wangle

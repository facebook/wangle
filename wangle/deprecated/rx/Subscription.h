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
#include <wangle/deprecated/rx/Observable.h>

namespace wangle {

template <class T>
class Subscription {
 public:
  Subscription() = default;

  Subscription(const Subscription&) = delete;

  Subscription(Subscription&& other) noexcept {
    *this = std::move(other);
  }

  Subscription& operator=(Subscription&& other) noexcept {
    unsubscribe();
    unsubscriber_ = std::move(other.unsubscriber_);
    id_ = other.id_;
    other.unsubscriber_ = nullptr;
    other.id_ = 0;
    return *this;
  }

  ~Subscription() {
    unsubscribe();
  }

 private:
  typedef typename Observable<T>::Unsubscriber Unsubscriber;

  Subscription(std::shared_ptr<Unsubscriber> unsubscriber, uint64_t id)
    : unsubscriber_(std::move(unsubscriber)), id_(id) {
    CHECK(id_ > 0);
  }

  void unsubscribe() {
    if (unsubscriber_ && id_ > 0) {
      unsubscriber_->unsubscribe(id_);
      id_ = 0;
      unsubscriber_ = nullptr;
    }
  }

  std::shared_ptr<Unsubscriber> unsubscriber_;
  uint64_t id_{0};

  friend class Observable<T>;
};

} // namespace wangle

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

namespace wangle {

/**
 * Subscriber interface for listening to a stream.
 */
template <typename T, typename R>
class Subscriber {
 public:
  virtual ~Subscriber() {}

  virtual void onNext(const T&) = 0;
  virtual void onError(folly::exception_wrapper ex) = 0;
  virtual void onCompleted() = 0;
  virtual R& routingData() = 0;
};

} // namespace wangle

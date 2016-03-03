/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
//
#pragma once

#include <chrono>

#include <folly/DynamicConverter.h>
#include <folly/FBString.h>

namespace wangle {

typedef struct SSLSessionCacheData {
  folly::fbstring sessionData;
  std::chrono::time_point<std::chrono::system_clock> addedTime;
  folly::fbstring serviceIdentity;
} SSLSessionCacheData;

} //proxygen

namespace folly {
  template<> folly::dynamic toDynamic(const wangle::SSLSessionCacheData& d);
  template<> wangle::SSLSessionCacheData convertTo(const dynamic& d);
} //folly

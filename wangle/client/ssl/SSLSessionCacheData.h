// Copyright 2004-present Facebook.  All rights reserved.
//
#pragma once


#include <folly/DynamicConverter.h>
#include <folly/FBString.h>
#include <string>
#include <chrono>

namespace wangle {

typedef struct SSLSessionCacheData {
  folly::fbstring sessionData;
  std::chrono::time_point<std::chrono::system_clock> addedTime;
} SSLSessionCacheData;

} //proxygen

namespace folly {
  template<> folly::dynamic toDynamic(const wangle::SSLSessionCacheData& d);
  template<> wangle::SSLSessionCacheData convertTo(const dynamic& d);
} //folly

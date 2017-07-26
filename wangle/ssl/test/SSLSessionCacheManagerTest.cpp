/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <glog/logging.h>
#include <folly/portability/GTest.h>
#include <wangle/ssl/SSLSessionCacheManager.h>
#include <folly/Random.h>

using std::shared_ptr;
using namespace folly;
using namespace wangle;

TEST(ShardedLocalSSLSessionCacheTest, TestHash) {
  uint32_t buckets = 10;
  uint32_t cacheSize = 20;
  uint32_t cacheCullSize = 100;

  std::array<uint8_t, 32> id;
  Random::secureRandom(id.data(), id.size());

  ShardedLocalSSLSessionCache cache(buckets, cacheSize, cacheCullSize);
  cache.hash(std::string((char*)id.data(), id.size()));
}

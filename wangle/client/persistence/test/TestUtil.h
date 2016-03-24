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

#include <cstdlib>
#include <list>
#include <unistd.h>
#include <vector>

#include <folly/Memory.h>
#include <gtest/gtest.h>
#include <wangle/client/persistence/FilePersistentCache.h>

namespace wangle {

std::string getPersistentCacheFilename();

template<typename K, typename V, typename MutexT = std::mutex>
void testSimplePutGet(
    const std::vector<K>& keys,
    const std::vector<V>& values) {
  std::string filename = getPersistentCacheFilename();
  typedef FilePersistentCache<K, V, MutexT> CacheType;
  size_t cacheCapacity = 10;
  {
    CacheType cache(filename, cacheCapacity, std::chrono::seconds(150));
    EXPECT_FALSE(cache.get(keys[0]).hasValue());
    EXPECT_FALSE(cache.get(keys[1]).hasValue());
    cache.put(keys[0], values[0]);
    cache.put(keys[1], values[1]);
    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.get(keys[0]).value(), values[0]);
    EXPECT_EQ(cache.get(keys[1]).value(), values[1]);
  }
  {
    CacheType cache(filename, cacheCapacity, std::chrono::seconds(150));
    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.get(keys[0]).value(), values[0]);
    EXPECT_EQ(cache.get(keys[1]).value(), values[1]);
    EXPECT_TRUE(cache.remove(keys[1]));
    EXPECT_FALSE(cache.remove(keys[1]));
    EXPECT_EQ(cache.size(), 1);
    EXPECT_EQ(cache.get(keys[0]).value(), values[0]);
    EXPECT_FALSE(cache.get(keys[1]).hasValue());
  }
  {
    CacheType cache(filename, cacheCapacity, std::chrono::seconds(150));
    EXPECT_EQ(cache.size(), 1);
    EXPECT_EQ(cache.get(keys[0]).value(), values[0]);
    EXPECT_FALSE(cache.get(keys[1]).hasValue());
    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_FALSE(cache.get(keys[0]).hasValue());
    EXPECT_FALSE(cache.get(keys[1]).hasValue());
  }
  {
    CacheType cache(filename, cacheCapacity, std::chrono::seconds(150));
    EXPECT_EQ(cache.size(), 0);
    EXPECT_FALSE(cache.get(keys[0]).hasValue());
    EXPECT_FALSE(cache.get(keys[1]).hasValue());
  }
  EXPECT_TRUE(unlink(filename.c_str()) != -1);
}

}

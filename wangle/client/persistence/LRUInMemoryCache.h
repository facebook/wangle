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

#include <tuple>

#include <folly/dynamic.h>
#include <folly/EvictingCacheMap.h>
#include <folly/Optional.h>
#include <wangle/client/persistence/PersistentCacheCommon.h>

namespace wangle {

/**
 * A threadsafe cache map that delegates to an EvictingCacheMap and maintains
 * a version of the data.
 */
template<typename K, typename V, typename MutexT>
class LRUInMemoryCache {
 public:
  /**
   * Create with the specified capacity.
   */
  explicit LRUInMemoryCache(size_t capacity) : cache_(capacity) {};
  ~LRUInMemoryCache() = default;

  folly::Optional<V> get(const K& key);
  void put(const K& key, const V& val);
  bool remove(const K& key);
  size_t size() const;
  void clear();

  CacheDataVersion getVersion() const;

  /**
   * Loads the list of kv pairs into the cache and bumps version.
   * Returns the new cache version.
   */
  CacheDataVersion loadData(const folly::dynamic& kvPairs) noexcept;

  /**
   * Get the cache data as a list of kv pairs along with the version
   */
  folly::Optional<std::tuple<folly::dynamic, CacheDataVersion>>
  convertToKeyValuePairs() noexcept;

  /**
   * Determine if the cache has changed since the specified version
   */
  bool hasChangedSince(CacheDataVersion version) const {
    return getVersion() != version;
  }

 private:

  // must be called under a write lock
  void incrementVersion() {
    ++version_;
    // if a uint64_t is incremented a billion times a second, it will still take
    // 585 years to wrap around, so don't bother
  }

  folly::EvictingCacheMap<K, V> cache_;
  // Version always starts at 1
  CacheDataVersion version_{1};
  // mutable so we can take read locks in const methods
  mutable MutexT cacheLock_;

};

}

#include <wangle/client/persistence/LRUInMemoryCache-inl.h>

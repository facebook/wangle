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

#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/ScopeGuard.h>
#include <folly/json.h>
#include <wangle/client/persistence/LRUPersistentCache.h>

namespace wangle {

/**
 * A PersistentCache implementation that used a regular file for
 * storage. In memory structure fronts the file and the cache
 * operations happen on it. Loading from and syncing to file are
 * hidden from clients. Sync to file happens asynchronously on
 * a separate thread at a configurable interval. Syncs to file
 * on destruction as well.
 *
 * NOTE NOTE NOTE: Although this class aims to be a cache for arbitrary,
 * it relies heavily on folly::toJson, folly::dynamic and convertTo for
 * serialization and deserialization. So It may not suit your need until
 * true support arbitrary types is written.
 */
template<typename K, typename V, typename M = std::mutex>
class FilePersistentCache : public PersistentCache<K, V>,
                            private boost::noncopyable {
 public:
  explicit FilePersistentCache(
    const std::string& file,
    const std::size_t cacheCapacity,
    const std::chrono::seconds& syncInterval = std::chrono::seconds(5),
    const int nSyncRetries = 3);

  ~FilePersistentCache() {}

  /**
   * PersistentCache operations
   */
  folly::Optional<V> get(const K& key) override {
    return cache_.get(key);
  }

  void put(const K& key, const V& val) override {
    cache_.put(key, val);
  }

  bool remove(const K& key) override {
    return cache_.remove(key);
  }

  void clear() override {
    cache_.clear();
  }

  size_t size() override {
    return cache_.size();
  }

 private:
  LRUPersistentCache<K, V, M> cache_;
};

} // namespace wangle

#include <wangle/client/persistence/FilePersistentCache-inl.h>

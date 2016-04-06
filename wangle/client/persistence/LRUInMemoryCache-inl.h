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

#include <folly/DynamicConverter.h>
#include <folly/Likely.h>

namespace wangle {

template<typename K, typename V, typename M>
folly::Optional<V> LRUInMemoryCache<K, V, M>::get(const K& key) {
  // need to take a write lock since get modifies the LRU
  typename wangle::CacheLockGuard<M>::Write writeLock(cacheLock_);
  auto itr = cache_.find(key);
  if (itr != cache_.end()) {
    return folly::Optional<V>(itr->second);
  }
  return folly::none;
}

template<typename K, typename V, typename M>
void LRUInMemoryCache<K, V, M>::put(const K& key, const V& val) {
  typename wangle::CacheLockGuard<M>::Write writeLock(cacheLock_);
  cache_.set(key, val);
  incrementVersion();
}

template<typename K, typename V, typename M>
bool LRUInMemoryCache<K, V, M>::remove(const K& key) {
  typename wangle::CacheLockGuard<M>::Write writeLock(cacheLock_);
  size_t nErased = cache_.erase(key);
  if (nErased > 0) {
    incrementVersion();
    return true;
  }
  return false;
}

template<typename K, typename V, typename M>
size_t LRUInMemoryCache<K, V, M>::size() const {
  typename wangle::CacheLockGuard<M>::Read readLock(cacheLock_);
  return cache_.size();
}

template<typename K, typename V, typename M>
void LRUInMemoryCache<K, V, M>::clear() {
  typename wangle::CacheLockGuard<M>::Write writeLock(cacheLock_);
  if (cache_.empty()) {
    return;
  }
  cache_.clear();
  incrementVersion();
}

template<typename K, typename V, typename M>
CacheDataVersion LRUInMemoryCache<K, V, M>::getVersion() const {
  typename wangle::CacheLockGuard<M>::Read readLock(cacheLock_);
  return version_;
}

template<typename K, typename V, typename M>
CacheDataVersion
LRUInMemoryCache<K, V, M>::loadData(const folly::dynamic& data) noexcept {
  bool updated = false;
  typename wangle::CacheLockGuard<M>::Write writeLock(cacheLock_);
  try {
    for (const auto& kv : data) {
      cache_.set(
        folly::convertTo<K>(kv[0]),
        folly::convertTo<V>(kv[1]));
      updated = true;
    }
  } catch (const folly::TypeError& err) {
    LOG(ERROR) << "Load cache failed with type error: "
                << err.what();
  } catch (const std::out_of_range& err) {
    LOG(ERROR) << "Load cache failed with key error: "
                << err.what();
  } catch (const std::exception& err) {
    LOG(ERROR) << "Load cache failed with error: "
                << err.what();
  }
  if (updated) {
    // we still need to increment the version
    incrementVersion();
  }
  return version_;
}

template<typename K, typename V, typename M>
folly::Optional<std::tuple<folly::dynamic, CacheDataVersion>>
LRUInMemoryCache<K, V, M>::convertToKeyValuePairs() noexcept {
  typename wangle::CacheLockGuard<M>::Read readLock(cacheLock_);
  try {
    folly::dynamic dynObj = folly::dynamic::array;
    for (const auto& kv : cache_) {
      dynObj.push_back(folly::toDynamic(std::make_pair(kv.first, kv.second)));
    }
    return std::make_tuple(std::move(dynObj), version_);
  } catch (const std::exception& err) {
    LOG(ERROR) << "Converting cache to folly::dynamic failed with error: "
               << err.what();
  }
  return folly::none;
}

}

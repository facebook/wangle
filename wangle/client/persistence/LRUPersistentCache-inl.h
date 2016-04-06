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

#include <cerrno>
#include <folly/DynamicConverter.h>
#include <folly/FileUtil.h>
#include <folly/json.h>
#include <folly/ScopeGuard.h>
#include <functional>
#include <sys/time.h>

namespace wangle {

template<typename K, typename V, typename MutexT>
LRUPersistentCache<K, V, MutexT>::LRUPersistentCache(
    const std::size_t cacheCapacity,
    const std::chrono::milliseconds& syncInterval,
    const int nSyncRetries,
    std::unique_ptr<CachePersistence<K, V>> persistence):
  cache_(cacheCapacity),
  pendingUpdates_(0),
  stopSyncer_(false),
  syncInterval_(syncInterval),
  nSyncRetries_(nSyncRetries),
  nSyncFailures_(0),
  persistence_(std::move(persistence)),
  syncer_(&LRUPersistentCache<K, V, MutexT>::syncThreadMain, this) {

  // load the cache. be silent if load fails, we just drop the cache
  // and start from scratch.
  if (persistence_) {
    load(*persistence_);
  }
}

template<typename K, typename V, typename MutexT>
LRUPersistentCache<K, V, MutexT>::~LRUPersistentCache() {
  {
    // tell syncer to wake up and quit
    std::lock_guard<std::mutex> lock(stopSyncerMutex_);

    stopSyncer_ = true;
    stopSyncerCV_.notify_all();
  }

  syncer_.join();
}

template<typename K, typename V, typename MutexT>
folly::Optional<V> LRUPersistentCache<K, V, MutexT>::get(const K& key) {
  typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);

  auto itr = cache_.find(key);
  if (itr != cache_.end()) {
    return folly::Optional<V>(itr->second);
  }
  return folly::Optional<V>();
}

template<typename K, typename V, typename MutexT>
void LRUPersistentCache<K, V, MutexT>::put(const K& key, const V& val) {
  typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);

  cache_.set(key, val);
  ++pendingUpdates_;
}

template<typename K, typename V, typename MutexT>
bool LRUPersistentCache<K, V, MutexT>::remove(const K& key) {
  typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);

  size_t nErased = cache_.erase(key);
  if (nErased > 0) {
    ++pendingUpdates_;
    return true;
  }
  return false;
}

template<typename K, typename V, typename MutexT>
bool LRUPersistentCache<K, V, MutexT>::hasPendingUpdates() {
  typename wangle::CacheLockGuard<MutexT>::Read readLock(cacheLock_);
  return pendingUpdates_ > 0;
}

template<typename K, typename V, typename MutexT>
void* LRUPersistentCache<K, V, MutexT>::syncThreadMain(void* arg) {
  auto self = static_cast<LRUPersistentCache<K, V, MutexT>*>(arg);
  self->sync();
  return nullptr;
}

template<typename K, typename V, typename MutexT>
void LRUPersistentCache<K, V, MutexT>::sync() {
  // keep running as long the destructor signals to stop or
  // there are pending updates that are not synced yet
  std::unique_lock<std::mutex> stopSyncerLock(stopSyncerMutex_);

  while (true) {
    if (stopSyncer_) {
      typename wangle::CacheLockGuard<MutexT>::Read readLock(cacheLock_);
      if (pendingUpdates_ == 0) {
        break;
      }
    }

    if (!syncNow()) {
      // track failures and give up if we tried too many times
      ++nSyncFailures_;
      LOG(ERROR) << "Persisting to cache failed " << nSyncFailures_ << " times";
      if (nSyncFailures_ == nSyncRetries_) {
        LOG(ERROR) << "Giving up after " << nSyncFailures_ << " failures";
        typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);
        pendingUpdates_ = 0;
        nSyncFailures_ = 0;
      }
    } else {
      nSyncFailures_ = 0;
    }

    if (!stopSyncer_) {
      stopSyncerCV_.wait_for(stopSyncerLock, syncInterval_);
    }
  }
}

template<typename K, typename V, typename MutexT>
bool LRUPersistentCache<K, V, MutexT>::syncNow() {
  folly::Optional<folly::dynamic> kvPairs;
  unsigned long queuedUpdates = 0;
  auto persistence = getPersistence();
  if (!persistence) {
    // this is considered a full sync
    typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);
    pendingUpdates_ = 0;
    return true;
  }

  // serialize the current contents of cache under lock
  {
    typename wangle::CacheLockGuard<MutexT>::Read readLock(cacheLock_);

    if (pendingUpdates_ == 0) {
      return true;
    }
    kvPairs = convertCacheToKvPairs();
    if (!kvPairs.hasValue()) {
      LOG(ERROR) << "Failed to convert cache to folly::dynamic";
      return false;
    }
    queuedUpdates = pendingUpdates_;
  }

  // do the actual persistence - no persistence = true
  bool persisted = persistence->persist(kvPairs.value());

  // if we succeeded in peristing, update pending update count
  if (persisted) {
    typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);
    // there's a chance that the persistence layer swapped out underneath
    // us.  this is ok and we will just sync on the next go around (or on
    // shutdown).  the pendingUpdates_ counter is incremented when the
    // persistence layer is set to inform the syncer thread that it needs
    // to sync.

    pendingUpdates_ -= queuedUpdates;
    DCHECK(pendingUpdates_ >= 0);
  } else {
    LOG(ERROR) << "Failed to persist " << queuedUpdates << " updates";
  }

  return persisted;
}

// serializes the cache_, must be called under read lock
template<typename K, typename V, typename MutexT>
folly::Optional<folly::dynamic>
LRUPersistentCache<K, V, MutexT>::convertCacheToKvPairs() {
  try {
    folly::dynamic dynObj = folly::dynamic::array;
    for (const auto& kv: cache_) {
      dynObj.push_back(folly::toDynamic(std::make_pair(kv.first, kv.second)));
    }
    return dynObj;
  } catch (const std::exception& err) {
    LOG(ERROR) << "Converting cache to folly::dynamic failed with error: "
               << err.what();
  }
  return folly::none;
}

template<typename K, typename V, typename MutexT>
std::shared_ptr<CachePersistence<K, V>>
LRUPersistentCache<K, V, MutexT>::getPersistence() {
  typename wangle::CacheLockGuard<MutexT>::Read readLock(persistenceLock_);
  return persistence_;
}

template<typename K, typename V, typename MutexT>
void LRUPersistentCache<K, V, MutexT>::setPersistence(
    std::unique_ptr<CachePersistence<K, V>> persistence) {
  {
    typename wangle::CacheLockGuard<MutexT>::Write writeLock(persistenceLock_);
    persistence_ = std::move(persistence);
    // load the persistence data into memory
    if (persistence_) {
      load(*persistence_);
    }
  }

  // mark the cache as updated so that it syncs to the underlying persistence
  // when the next sync happens.  this is for the case when the in memory cache
  // has data that may not be in the underlying persistence.
  {
    typename wangle::CacheLockGuard<MutexT>::Write wh(cacheLock_);
    ++pendingUpdates_;
  }
}

template<typename K, typename V, typename MutexT>
bool
LRUPersistentCache<K, V, MutexT>::loadCache(const folly::dynamic& kvPairs) {
  bool error = true;
  typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);
  try {
    for (const auto& kv : kvPairs) {
      cache_.set(
        folly::convertTo<K>(kv[0]),
        folly::convertTo<V>(kv[1])
      );
    }
    error = false;
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

  // we don't clear the existing cache on load error.  there are two scenarios:
  // 1 - initial call to load in constructor.  the cache is already empty so a
  // clear is effectively a noop.
  // 2 - call to load when persistence changes.  the cache may already have some
  // entries in memory so blowing them away would nuke valid entries.
  // instead we will just sync them down to this persistence layer on the next
  // sync.
  return !error;
}

template<typename K, typename V, typename MutexT>
bool LRUPersistentCache<K, V, MutexT>::load(
    CachePersistence<K, V>& persistence) noexcept {
  auto kvPairs = persistence.load();
  if (!kvPairs) {
    return false;
  }
  return loadCache(kvPairs.value());
}

template<typename K, typename V, typename MutexT>
void LRUPersistentCache<K, V, MutexT>::clear() {
  typename wangle::CacheLockGuard<MutexT>::Write writeLock(cacheLock_);

  cache_.clear();
  ++pendingUpdates_;
}

template<typename K, typename V, typename MutexT>
size_t LRUPersistentCache<K, V, MutexT>::size() {
  typename wangle::CacheLockGuard<MutexT>::Read readLock(cacheLock_);

  return cache_.size();
}

}

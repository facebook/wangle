/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <thread>

#include <boost/noncopyable.hpp>
#include <folly/dynamic.h>
#include <wangle/client/persistence/LRUInMemoryCache.h>
#include <wangle/client/persistence/PersistentCache.h>
#include <wangle/client/persistence/PersistentCacheCommon.h>

namespace wangle {

/**
 * The underlying persistence layer interface.  Implementations may
 * write to file, db, /dev/null, etc.
 */
template<typename K, typename V>
class CachePersistence {
 public:
  CachePersistence() : persistedVersion_(0) {}

  virtual ~CachePersistence() = default;

  /**
   * Persist a folly::dynamic array of key value pairs at the
   * specified version.  Returns true if persistence succeeded.
   */
  bool persistVersionedData(
      const folly::dynamic& kvPairs, const CacheDataVersion& version) {
    auto result = persist(kvPairs);
    if (result) {
      persistedVersion_ = version;
    }
    return result;
  }

  /**
   * Get the last version of the data that was successfully persisted.
   */
  virtual CacheDataVersion getLastPersistedVersion() const {
    return persistedVersion_;
  }

  /**
   * Force set a persisted version.  This is primarily for when a persistence
   * layer acts as the initial source of data for some version tracking cache.
   */
  void setPersistedVersion(CacheDataVersion version) noexcept {
    persistedVersion_ = version;
  }

  /**
   * Persist a folly::dynamic array of key value pairs.
   * Returns true on success.
   */
  virtual bool persist(const folly::dynamic& kvPairs) noexcept = 0;

  /**
   * Returns a list of key value pairs that are present in this
   * persistence store.
   */
  virtual folly::Optional<folly::dynamic> load() noexcept = 0;

  /**
   * Clears Persistent cache
   */
  virtual void clear() = 0;

 private:
  CacheDataVersion persistedVersion_;
};

/**
 * A PersistentCache implementation that used a CachePersistence for
 * storage. In memory structure fronts the persistence and the cache
 * operations happen on it. Loading from and syncing to persistence are
 * hidden from clients. Sync to persistence happens asynchronously on
 * a separate thread at a configurable interval. Syncs to persistence
 * on destruction as well.
 *
 * The in memory structure is an EvictingCacheMap which causes this class
 * to evict entries in an LRU fashion.
 *
 * NOTE NOTE NOTE: Although this class aims to be a cache for arbitrary,
 * it relies heavily on folly::toJson, folly::dynamic and convertTo for
 * serialization and deserialization. So It may not suit your need until
 * true support arbitrary types is written.
 */
template<typename K, typename V, typename MutexT = std::mutex>
class LRUPersistentCache : public PersistentCache<K, V>,
                           private boost::noncopyable {
 public:
  /**
   * LRUPersistentCache constructor
   * @param cacheCapacity max number of elements to hold in the cache.
   * @param syncInterval how often to sync to the persistence (in ms).
   * @param nSyncRetries how many times to retry to sync on failure.
   *
   * Loads the cache and starts of the syncer thread that periodically
   * syncs the cache to persistence.
   *
   * If persistence is specified, the cache is initially loaded with the
   * contents from it. If load fails, then cache starts empty.
   *
   * On write failures, the sync will happen again up to nSyncRetries times.
   * Once failed nSyncRetries amount of time, then it will give up and not
   * attempt to sync again until another update occurs.
   *
   * On reaching capacity limit, LRU items are evicted.
   */
  explicit LRUPersistentCache(
    const std::size_t cacheCapacity,
    const std::chrono::milliseconds& syncInterval =
    std::chrono::milliseconds(5000),
    const int nSyncRetries = 3,
    std::unique_ptr<CachePersistence<K, V>> persistence = nullptr
  );

  /**
   * LRUPersistentCache Destructor
   *
   * Signals the syncer thread to stop, waits for any pending syncs to
   * be done.
   */
  ~LRUPersistentCache() override;

  /**
   * Check if there are updates that need to be synced to persistence
   */
  bool hasPendingUpdates();

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

  void clear(bool clearPersistence = false) override {
    cache_.clear();
    if (clearPersistence) {
      auto persistence = getPersistence();
      if (persistence) {
        persistence->clear();
      }
    }
  }

  size_t size() override {
    return cache_.size();
  }

  /**
   * Set a new persistence layer on this cache.  This call blocks while the
   * new persistence layer is loaded into the cache.  The load is also
   * done under a lock so multiple calls to this will not stomp on each
   * other.
   */
  void setPersistence(std::unique_ptr<CachePersistence<K, V>> persistence);

 private:
  /**
   * Helper to set persistence that will load the persistence data
   * into memory and optionally sync versions
   */
  void setPersistenceHelper(
    std::unique_ptr<CachePersistence<K, V>> persistence,
    bool syncVersion) noexcept;

  /**
   * Load the contents of the persistence passed to constructor in to the
   * in-memory cache. Failure to read will result in no changes to the
   * in-memory data.  That is, if in-memory entries exist, and loading
   * fails, the in-memory data remains and will sync down to the underlying
   * persistence layer on the next sync.
   *
   * Failure to read inclues IO errors and deserialization errors.
   *
   * @returns the in memory cache's new version
   */
  CacheDataVersion load(CachePersistence<K, V>& persistence) noexcept;

  /**
   * The syncer thread's function. Syncs to the persistence, if necessary,
   * after every syncInterval_ seconds.
   */
  void sync();
  static void* syncThreadMain(void* arg);

  /**
   * Helper to sync routine above that actualy does the serialization
   * and writes to persistence.
   *
   * @returns boolean, true on successful serialization and write to
   *                    persistence, false otherwise
   */
  bool syncNow(CachePersistence<K, V>& persistence);

  /**
   * Helper to get the persistence layer under lock since it will be called
   * by syncer thread and setters call from any thread.
   */
  std::shared_ptr<CachePersistence<K, V>> getPersistence();

 private:

  // Our threadsafe in memory cache
  LRUInMemoryCache<K, V, MutexT> cache_;

  // used to signal syncer thread
  bool stopSyncer_;
  // mutex used to synchronize syncer_ on destruction, tied to stopSyncerCV_
  std::mutex stopSyncerMutex_;
  // condvar used to wakeup syncer on exit
  std::condition_variable stopSyncerCV_;

  // sync interval in milliseconds
  const std::chrono::milliseconds syncInterval_;
  // limit on no. of sync attempts
  const int nSyncRetries_;

  // persistence layer
  // we use a shared pointer since the syncer thread might be operating on
  // it when the client decides to set a new one
  std::shared_ptr<CachePersistence<K, V>> persistence_;
  // for locking access to persistence set/get
  MutexT persistenceLock_;

  // thread for periodic sync
  std::thread syncer_;
};

}

#include <wangle/client/persistence/LRUPersistentCache-inl.h>

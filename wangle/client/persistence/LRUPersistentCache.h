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

#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <thread>

#include <boost/noncopyable.hpp>
#include <folly/EvictingCacheMap.h>
#include <folly/dynamic.h>
#include <wangle/client/persistence/PersistentCache.h>

namespace wangle {

/**
 * A guard that provides write and read access to a mutex type.
 */
template<typename MutexT>
struct CacheLockGuard;

// Specialize on std::mutex by providing exclusive access
template<>
struct CacheLockGuard<std::mutex> {
  using Read = std::lock_guard<std::mutex>;
  using Write = std::lock_guard<std::mutex>;
};

/**
 * The underlying persistence layer interface.  Implementations may
 * write to file, db, /dev/null, etc.
 */
template<typename K, typename V>
class CachePersistence {
 public:
  virtual ~CachePersistence() {}

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
  static_assert(std::is_convertible<K, folly::dynamic>::value &&
                std::is_convertible<V, folly::dynamic>::value,
                "Key and Value types must be convertible to dynamic");

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
  folly::Optional<V> get(const K& key) override;
  void put(const K& key, const V& val) override;
  bool remove(const K& key) override;
  void clear() override;
  size_t size() override;

  /**
   * Set a new persistence layer on this cache.  This call blocks while the
   * new persistence layer is loaded into the cache.  The load is also
   * done under a lock so multiple calls to this will not stomp on each
   * other.
   */
  void setPersistence(std::unique_ptr<CachePersistence<K, V>> persistence);

 private:
  /**
   * Load the contents of the persistence passed to constructor in to the
   * in-memory cache. Failure to read will result in no changes to the
   * in-memory data.  That is, if in-memory entries exist, and loading
   * fails, the in-memory data remains and will sync down to the underlying
   * persistence layer on the next sync.
   *
   * Failure to read inclues IO errors and deserialization errors.
   *
   * @returns boolean, true on successful load, false otherwise
   */
  bool load(CachePersistence<K, V>& persistence) noexcept;

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
   * @returns boolean, true on successful serialization and write to persistence
   *                    persistence, false otherwise
   */
  bool syncNow();

  /**
   * Helper to syncNow routine above that converts cache_ to a
   * folly::dynamic list of K,V pairs.
   *
   * @returns Optional<folly::dynamic>, the list of K,V pairs if succeeded,
   *                                     no value on failure
   */
  folly::Optional<folly::dynamic> convertCacheToKvPairs();

  /**
   * Helper to load routine above that loads the cache from a folly::dynamic
   * list of K, V pairs
   *
   * @param kvPairs, the list of K,V pairs
   *
   * @returns boolean, true if deserialize succeeded,
   *                    false on failure
   */
  bool loadCache(const folly::dynamic& kvPairs);

  /**
   * Helper to get the persistence layer under lock since it will be called
   * by syncer thread and setters call from any thread.
   */
  std::shared_ptr<CachePersistence<K, V>> getPersistence();

 private:

  // pendingUpdates_ below is really tied to cache_, so modify them together
  // always under the same lock

  // in-memory LRU evicting cache table
  folly::EvictingCacheMap<K, V> cache_;
  // tracks pendingUpdates_
  unsigned long pendingUpdates_;
  // for locking cache_ and pendingUpdates_
  MutexT cacheLock_;

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
  // tracks no. of consecutive sync failures
  int nSyncFailures_;

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

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
template<typename K, typename V, typename MutexT = std::mutex>
class FilePersistentCache : public PersistentCache<K, V>,
                            private boost::noncopyable {
  static_assert(std::is_convertible<K, folly::dynamic>::value &&
                std::is_convertible<V, folly::dynamic>::value,
      "Key and Value types must be convertible to dynamic");

  public:
    /**
     * FilePersistentCache constructor
     * @param file path to the file to use as storage.
     * @param cacheCapacity max number of elements to hold in the cache.
     * @param syncInterval how often to sync to the file (in seconds).
     * @param nSyncRetries how many times to retry to sync on failure.
     *
     * Loads the cache and starts of the syncer thread that periodically
     * syncs the cache to file.
     *
     * Its not necessary that file exists. Its contents are ignored if
     * deserialization fails. Cache starts out empty in that case. On each
     * sync operation the file gets overwritten. Write failures are ignored
     * in which case the in memory copy and file will get out of sync.
     * On nSyncRetries failures, ignores all current updates to in-memory
     * copy because we dont want to keep trying forever.
     *
     * On reaching capacity limit, LRU items are evicted.
     */
    explicit FilePersistentCache(const std::string& file,
        const std::size_t cacheCapacity,
        const std::chrono::seconds& syncInterval = std::chrono::seconds(5),
        const int nSyncRetries = 3);

    /**
     * FilePersistentCache Destructor
     *
     * Signals the syncer thread to stop, waits for any pending syncs to
     * be done.
     */
    ~FilePersistentCache() override;

    /**
     * PersistentCache operations
     */
    folly::Optional<V> get(const K& key) override;
    void put(const K& key, const V& val) override;
    bool remove(const K& key) override;
    void clear() override;
    size_t size() override;

  private:
    /**
     * Load the contents of the file passed to constructor in to the
     * in-memory cache. Failure to read will result in an empty cache.
     * Failure to read inclues IO errors and deserialization errors.
     *
     * @returns boolean, true on successful load, false otherwise
     */
    bool load() noexcept;

    /**
     * The syncer thread's function. Syncs to the file, if necessary,
     * after every syncInterval_ seconds.
     */
    void sync();
    static void* syncThreadMain(void* arg);

    /**
     * Helper to sync routine above that actualy does the serialization
     * and writes to file.
     *
     * @returns boolean, true on successful serialization and write to file,
     *                    false otherwise
     */
    bool syncNow();

    /**
     * Helper to syncNow routine above that serializes data
     *
     * Attempts to serialize cache_. Uses toDynamic and toJson from folly.
     *
     * @returns Optional<std::string>, the string if serialization succeeded, no
     *                            value on failure
     */
    folly::Optional<std::string> serializeCache();

    /**
     * Helper to load routine above that deserializes data
     *
     * @param serializedCache string, the serialized cache
     *
     * Attempts to deserialize data. Uses parseJson from folly.
     *
     * @returns boolean, true if deserialize succeeded,
     *                    false on failure
     */
    bool deserializeCache(const std::string& serializedCache);

    /**
     * Helper to syncNow routine above that actualy writes to the underlying
     * file.
     * @param serializedCache string, the serialized cache
     *
     * @returns boolean, true on successful write to file, false otherwise
     */
    bool persist(std::string&& serializedCache);

  private:
    // path to the file on disk
    const std::string file_;

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

    // sync interval in seconds
    const std::chrono::seconds syncInterval_;
    // limit on no. of sync attempts
    const int nSyncRetries_;
    // tracks no. of consecutive sync failures
    int nSyncFailures_;

    // thread for periodic sync
    std::thread syncer_;
};

}

#include <wangle/client/persistence/FilePersistentCache-inl.h>

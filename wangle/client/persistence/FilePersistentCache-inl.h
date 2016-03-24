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

template<typename K, typename V>
FilePersistentCache<K, V>::FilePersistentCache(const std::string& file,
    const std::size_t cacheCapacity,
    const std::chrono::seconds& syncInterval,
    const int nSyncRetries):
  file_(file),
  cache_(cacheCapacity),
  pendingUpdates_(0),
  stopSyncer_(false),
  syncInterval_(syncInterval),
  nSyncRetries_(nSyncRetries),
  nSyncFailures_(0),
  syncer_(&FilePersistentCache<K, V>::syncThreadMain, this) {

  // load the cache. be silent if load fails, we just drop the cache
  // and start from scratch.
  load();
}

template<typename K, typename V>
FilePersistentCache<K, V>::~FilePersistentCache() {
  {
    // tell syncer to wake up and quit
    std::lock_guard<std::mutex> lock(stopSyncerMutex_);

    stopSyncer_ = true;
    stopSyncerCV_.notify_all();
  }

  syncer_.join();
}

template<typename K, typename V>
folly::Optional<V> FilePersistentCache<K, V>::get(const K& key) {
  std::lock_guard<std::mutex> lock(cacheLock_);

  auto itr = cache_.find(key);
  if (itr != cache_.end()) {
    return folly::Optional<V>(itr->second);
  }
  return folly::Optional<V>();
}

template<typename K, typename V>
void FilePersistentCache<K, V>::put(const K& key, const V& val) {
  std::lock_guard<std::mutex> lock(cacheLock_);

  cache_.set(key, val);
  ++pendingUpdates_;
}

template<typename K, typename V>
bool FilePersistentCache<K, V>::remove(const K& key) {
  std::lock_guard<std::mutex> lock(cacheLock_);

  size_t nErased = cache_.erase(key);
  if (nErased > 0) {
    ++pendingUpdates_;
    return true;
  }
  return false;
}

template<typename K, typename V>
void* FilePersistentCache<K, V>::syncThreadMain(void* arg) {
  auto self = static_cast<FilePersistentCache<K, V>*>(arg);
  self->sync();
  return nullptr;
}

template<typename K, typename V>
void FilePersistentCache<K, V>::sync() {
  // keep running as long the destructor signals to stop or
  // there are pending updates that are not synced yet
  std::unique_lock<std::mutex> stopSyncerLock(stopSyncerMutex_);

  while (true) {
    if (stopSyncer_) {
      std::lock_guard<std::mutex> lock(cacheLock_);
      if (pendingUpdates_ == 0) {
        break;
      }
    }

    if (!syncNow()) {
      LOG(ERROR) << "Persisting to cache failed " << nSyncFailures_ << " times";
      // track failures and give up if we tried too many times
      ++nSyncFailures_;
      if (nSyncFailures_ == nSyncRetries_) {
        LOG(ERROR) << "Giving up after " << nSyncFailures_ << " failures";
        std::lock_guard<std::mutex> lock(cacheLock_);
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

template<typename K, typename V>
bool FilePersistentCache<K, V>::syncNow() {
  folly::Optional<std::string> serializedCache;
  unsigned long queuedUpdates = 0;
  // serialize the current contents of cache under lock
  {
    std::lock_guard<std::mutex> lock(cacheLock_);

    if (pendingUpdates_ == 0) {
      return true;
    }
    serializedCache = serializeCache();
    if (!serializedCache.hasValue()) {
      LOG(ERROR) << "Failed to serialize cache";
      return false;
    }
    queuedUpdates = pendingUpdates_;
  }

  // do the actual file write
  bool persisted = persist(std::move(serializedCache.value()));

  // if we succeeded in peristing, update pending update count
  if (persisted) {
    std::lock_guard<std::mutex> lock(cacheLock_);

    pendingUpdates_ -= queuedUpdates;
    DCHECK(pendingUpdates_ >= 0);
  } else {
    LOG(ERROR) << "Failed to persist " << queuedUpdates << " updates";
  }

  return persisted;
}

// serializes the cache_, must be called under lock
template<typename K, typename V>
folly::Optional<std::string> FilePersistentCache<K, V>::serializeCache() {
  try {
    folly::dynamic dynObj = folly::dynamic::array;
    for (const auto& kv : cache_) {
      dynObj.push_back(folly::toDynamic(std::make_pair(kv.first, kv.second)));
    }
    folly::json::serialization_opts opts;
    opts.allow_non_string_keys = true;
    auto serializedCache = folly::json::serialize(dynObj, opts).toStdString();

    return folly::Optional<std::string>(std::move(serializedCache));
  } catch (const std::exception& err) {
    LOG(ERROR) << "Serialization of cache failed with parse error: "
                << err.what();
  }
  return folly::Optional<std::string>();
}

template<typename K, typename V>
bool FilePersistentCache<K, V>::deserializeCache(
    const std::string& serializedCache) {
  folly::Optional<folly::dynamic> cacheFromString;
  try {
    folly::json::serialization_opts opts;
    opts.allow_non_string_keys = true;
    cacheFromString = folly::parseJson(serializedCache, opts);
  } catch (const std::exception& err) {
    LOG(ERROR) << "Deserialization of cache failed with parse error: "
                << err.what();

    // If parsing fails we haven't taken the lock yet so do so here.
    std::lock_guard<std::mutex> lock(cacheLock_);

    cache_.clear();
    return false;
  }

  bool error = true;
  DCHECK(cacheFromString);
  std::lock_guard<std::mutex> lock(cacheLock_);

  try {
    for (const auto& kv : *cacheFromString) {
      cache_.set(
        folly::convertTo<K>(kv[0]),
        folly::convertTo<V>(kv[1])
      );
    }
    error = false;
  } catch (const folly::TypeError& err) {
    LOG(ERROR) << "Deserialization of cache failed with type error: "
                << err.what();

  } catch (const std::out_of_range& err) {
    LOG(ERROR) << "Deserialization of cache failed with key error: "
                << err.what();

  } catch (const std::exception& err) {
    LOG(ERROR) << "Deserialization of cache failed with error: "
                << err.what();

  }

  if (error) {
    cache_.clear();
    return false;
  }

  return true;
}

template<typename K, typename V>
bool FilePersistentCache<K, V>::persist(std::string&& serializedCache) {
  bool persisted = false;
  const auto fd = folly::openNoInt(
                    file_.c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IWUSR
                  );
  if (fd == -1) {
    LOG(ERROR) << "Failed to open " << file_ << ": errno " << errno;
    return false;
  }
  const auto nWritten = folly::writeFull(
                          fd,
                          serializedCache.data(),
                          serializedCache.size()
                        );
  persisted = nWritten >= 0 &&
    (static_cast<size_t>(nWritten) == serializedCache.size());
  if (!persisted) {
    LOG(ERROR) << "Failed to write to " << file_ << ":";
    if (nWritten == -1) {
      LOG(ERROR) << "write failed with errno " << errno;
    }
  }
  if (folly::closeNoInt(fd) != 0) {
    LOG(ERROR) << "Failed to close " << file_ << ": errno " << errno;
    persisted = false;
  }
  return persisted;
}

template<typename K, typename V>
bool FilePersistentCache<K, V>::load() noexcept {
  std::string serializedCache;
  // not being able to read the backing storage means we just
  // start with an empty cache. Failing to deserialize, or write,
  // is a real error so we report errors there.
  if (!folly::readFile(file_.c_str(), serializedCache)){
    return false;
  }

  if (deserializeCache(serializedCache)) {
    return true;
  } else {
    LOG(ERROR) << "Deserialization of cache failed ";
  }
  return false;
}

template<typename K, typename V>
void FilePersistentCache<K, V>::clear() {
  std::lock_guard<std::mutex> lock(cacheLock_);

  cache_.clear();
  ++pendingUpdates_;
}

template<typename K, typename V>
size_t FilePersistentCache<K, V>::size() {
  std::lock_guard<std::mutex> lock(cacheLock_);

  return cache_.size();
}

}

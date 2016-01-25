// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <cerrno>
#include <folly/DynamicConverter.h>
#include <folly/FileUtil.h>
#include <folly/json.h>
#include <folly/ScopeGuard.h>
#include <functional>
#include <sys/time.h>

// Can't use do {} while (0) trick here since that will create a scope and
// break SCOPE_EXIT.
#define LOCK_FPC_MUTEX_WITH_SCOPEGUARD(mutex)               \
  ec = pthread_mutex_lock(&mutex);                          \
  CHECK_EQ(0, ec) << "Failed to lock " << #mutex;           \
  SCOPE_EXIT {                                              \
    ec = pthread_mutex_unlock(&mutex);                      \
    CHECK_EQ(0, ec) << "Failed to unlock " << #mutex;       \
  }

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
  nSyncFailures_(0) {
  int ec;
  ec = pthread_mutex_init(&cacheLock_, nullptr);
  CHECK_EQ(0, ec) << "Failed to initialize cacheLock_";

  ec = pthread_mutex_init(&stopSyncerMutex_, nullptr);
  CHECK_EQ(0, ec) << "Failed to initialize stopSyncerMutex_";

  ec = pthread_cond_init(&stopSyncerCV_, nullptr);
  CHECK_EQ(0, ec) << "Failed to initialize stopSyncerCV_";

  ec = pthread_create(&syncer_, nullptr,
          &FilePersistentCache<K, V>::syncThreadMain, this);
  CHECK_EQ(0, ec) << "Failed to create syncer thread for " << file_;

  // load the cache. be silent if load fails, we just drop the cache
  // and start from scratch.
  load();
}

template<typename K, typename V>
FilePersistentCache<K, V>::~FilePersistentCache() {
  int ec;

  {
    // tell syncer to wake up and quit
    LOCK_FPC_MUTEX_WITH_SCOPEGUARD(stopSyncerMutex_);

    stopSyncer_ = true;
    ec = pthread_cond_broadcast(&stopSyncerCV_);
    CHECK_EQ(0, ec) << "Failed to notify stopSyncerCV_";
  }

  // Most pthread_join(3) failures are not fatal (e.g. thread has already
  // terminated). EDEADLK would be unexpected, though so crash hard on that.
  ec = pthread_join(syncer_, nullptr);
  LOG_IF(WARNING, ec != 0) << "Failed to join syncer thread: " << ec;
  CHECK_NE(EDEADLK, ec);

  ec = pthread_cond_destroy(&stopSyncerCV_);
  LOG_IF(WARNING, ec != 0) << "Failed to destroy stopSyncerCV_: " << ec;
  ec = pthread_mutex_destroy(&stopSyncerMutex_);
  LOG_IF(WARNING, ec != 0) << "Failed to destroy stopSyncerMutex_: " << ec;
  ec = pthread_mutex_destroy(&cacheLock_);
  LOG_IF(WARNING, ec != 0) << "Failed to destroy cacheLock_: " << ec;
}

template<typename K, typename V>
folly::Optional<V> FilePersistentCache<K, V>::get(const K& key) {
  int ec;
  LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

  auto itr = cache_.find(key);
  if (itr != cache_.end()) {
    return folly::Optional<V>(itr->second);
  }
  return folly::Optional<V>();
}

template<typename K, typename V>
void FilePersistentCache<K, V>::put(const K& key, const V& val) {
  int ec;
  LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

  cache_.set(key, val);
  ++pendingUpdates_;
}

template<typename K, typename V>
bool FilePersistentCache<K, V>::remove(const K& key) {
  int ec;
  LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

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
  int ec;
  LOCK_FPC_MUTEX_WITH_SCOPEGUARD(stopSyncerMutex_);

  while (true) {
    if (stopSyncer_) {
      LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);
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
        LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);
        pendingUpdates_ = 0;
        nSyncFailures_ = 0;
      }
    } else {
      nSyncFailures_ = 0;
    }

    if (!stopSyncer_) {
      timeval tv;
      ec = gettimeofday(&tv, nullptr);
      CHECK_EQ(0, ec);

      timespec ts;
      ts.tv_sec = tv.tv_sec + folly::to<time_t>(syncInterval_.count());
      ts.tv_nsec = 0;
      ec = pthread_cond_timedwait(&stopSyncerCV_, &stopSyncerMutex_, &ts);
      CHECK_NE(EINVAL, ec);
    }
  }
}

template<typename K, typename V>
bool FilePersistentCache<K, V>::syncNow() {
  folly::Optional<std::string> serializedCache;
  unsigned long queuedUpdates = 0;
  int ec;
  // serialize the current contents of cache under lock
  {
    LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

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
    LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

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
    folly::dynamic dynObj({});
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
  int ec;
  try {
    folly::json::serialization_opts opts;
    opts.allow_non_string_keys = true;
    cacheFromString = folly::parseJson(serializedCache, opts);
  } catch (const std::exception& err) {
    LOG(ERROR) << "Deserialization of cache failed with parse error: "
                << err.what();

    // If parsing fails we haven't taken the lock yet so do so here.
    LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

    cache_.clear();
    return false;
  }

  bool error = true;
  DCHECK(cacheFromString);
  LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

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
bool FilePersistentCache<K, V>::load() {
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
  int ec;
  LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

  cache_.clear();
  ++pendingUpdates_;
}

template<typename K, typename V>
size_t FilePersistentCache<K, V>::size() {
  int ec;
  LOCK_FPC_MUTEX_WITH_SCOPEGUARD(cacheLock_);

  return cache_.size();
}

}

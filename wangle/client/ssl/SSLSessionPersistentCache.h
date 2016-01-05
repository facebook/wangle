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

#include <atomic>
#include <chrono>
#include <map>
#include <openssl/ssl.h>
#include <folly/Memory.h>
#include <wangle/client/persistence/PersistentCache.h>
#include <wangle/client/ssl/SSLSessionCacheData.h>
#include <wangle/client/ssl/SSLSession.h>
#include <wangle/client/ssl/SSLSessionCallbacks.h>

namespace wangle {

/**
 * This cache is as threadsafe as the underlying PersistentCache used.
 * Multiple instances may delegate to the same persistence layer
 */
template<typename K>
class SSLSessionPersistentCacheBase: public SSLSessionCallbacks {

 public:
  class TimeUtil {
   public:
     virtual ~TimeUtil() {}

     virtual std::chrono::time_point<std::chrono::system_clock> now() const {
       return std::chrono::system_clock::now();
     }
  };

  explicit SSLSessionPersistentCacheBase(
    std::shared_ptr<PersistentCache<K, SSLSessionCacheData>> cache,
    bool doTicketLifetimeExpiration = false);

  explicit SSLSessionPersistentCacheBase(
    const std::string& filename,
    const std::size_t cacheCapacity,
    const std::chrono::seconds& syncInterval,
    bool doTicketLifetimeExpiration = false);

  // Store the session data of the specified hostname in cache. Note that the
  // implementation must make it's own memory copy of the session data to put
  // into the cache.
  void setSSLSession(
    const std::string& hostname, SSLSessionPtr session) noexcept override;

  // Return a SSL session if the cache contained session information for the
  // specified hostname. It is the caller's responsibility to decrement the
  // reference count of the returned session pointer.
  SSLSessionPtr getSSLSession(
    const std::string& hostname) const noexcept override;

  // Remove session data of the specified hostname from cache. Return true if
  // there was session data associated with the hostname before removal, or
  // false otherwise.
  bool removeSSLSession(const std::string& hostname) noexcept override;

  // Return true if the underlying cache supports persistence
  bool supportsPersistence() const noexcept override {
    return true;
  }

  void enableTicketLifetimeExpiration(bool val) noexcept {
    enableTicketLifetimeExpiration_ = val;
  }

  void setTimeUtil(std::unique_ptr<TimeUtil> timeUtil) noexcept {
    timeUtil_ = std::move(timeUtil);
  }

  // For test only, returns the number of entries in the cache.
  size_t size() const override;

 protected:
  // Get the persistence key from the session's hostname
  virtual K getKey(const std::string& hostname) const = 0;

  std::shared_ptr<PersistentCache<K, SSLSessionCacheData>>
    persistentCache_;
  std::atomic<bool> enableTicketLifetimeExpiration_;
  std::unique_ptr<TimeUtil> timeUtil_;
};

class SSLSessionPersistentCache :
      public SSLSessionPersistentCacheBase<std::string> {
 public:
  SSLSessionPersistentCache(
    const std::string& filename,
    const std::size_t cacheCapacity,
    const std::chrono::seconds& syncInterval,
    bool doTicketLifetimeExpiration = false) :
      SSLSessionPersistentCacheBase(
        filename, cacheCapacity, syncInterval, doTicketLifetimeExpiration) {}

 protected:
  std::string getKey(const std::string& hostname) const override {
    return hostname;
  }

};
}

#include <wangle/client/ssl/SSLSessionPersistentCache-inl.h>

// Copyright 2004-present Facebook.  All rights reserved.
//
#pragma once

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
 * This cache is not thread-safe, so it is not meant to be used among multiple
 * threads.
 */
class SSLSessionPersistentCache : public SSLSessionCallbacks {

 public:
  class TimeUtil {
   public:
     virtual ~TimeUtil() {}

     virtual std::chrono::time_point<std::chrono::system_clock> now() const {
       return std::chrono::system_clock::now();
     }
  };

  explicit SSLSessionPersistentCache(
    const std::string& filename, std::size_t cacheCapacity,
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
  bool supportsPersistence() noexcept override {
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
  std::unique_ptr<PersistentCache<std::string, SSLSessionCacheData>>
    persistentCache_;
  bool enableTicketLifetimeExpiration_;
  std::unique_ptr<TimeUtil> timeUtil_;
};

}

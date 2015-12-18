/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/client/ssl/SSLSessionPersistentCache.h>
#include <wangle/client/ssl/SSLSessionCacheUtils.h>
#include <wangle/client/persistence/FilePersistentCache.h>
#include <folly/io/IOBuf.h>
#include <folly/Memory.h>

namespace wangle {

SSLSessionPersistentCache::SSLSessionPersistentCache(
  const std::string& filename,
  const std::size_t cacheCapacity,
  const std::chrono::seconds& syncInterval,
  bool doTicketLifetimeExpiration) :
  persistentCache_(
      new FilePersistentCache<std::string, SSLSessionCacheData>(
        filename,
        cacheCapacity,
        syncInterval)),
  enableTicketLifetimeExpiration_(doTicketLifetimeExpiration),
  timeUtil_(new TimeUtil()) {}

void SSLSessionPersistentCache::setSSLSession(
    const std::string& hostname, SSLSessionPtr session) noexcept {
  if (!session) {
    return;
  }

  // We do not cache the session itself, but cache the session data from it in
  // order to recreate a new session later.
  auto sessionData = sessionToFbString(session.get());
  if (sessionData) {
    SSLSessionCacheData data;
    data.sessionData = std::move(*sessionData);
    persistentCache_->put(hostname, data);

    data.addedTime = timeUtil_->now();
  }
}

SSLSessionPtr
SSLSessionPersistentCache::getSSLSession(
    const std::string& hostname) const noexcept {
  auto hit = persistentCache_->get(hostname);
  if (!hit) {
    return nullptr;
  }

  // Create a SSL_SESSION and return. In failure it returns nullptr.
  auto& value = hit.value();
  auto sess = SSLSessionPtr(fbStringToSession(value.sessionData));

#if OPENSSL_TICKETS
  if (enableTicketLifetimeExpiration_ &&
      sess &&
      sess->tlsext_ticklen > 0 &&
      sess->tlsext_tick_lifetime_hint > 0) {
    auto now = timeUtil_->now();
    auto secsBetween =
      std::chrono::duration_cast<std::chrono::seconds>(now - value.addedTime);
    if (secsBetween >= std::chrono::seconds(sess->tlsext_tick_lifetime_hint)) {
      return nullptr;
    }
  }
#endif

  return sess;
}

bool SSLSessionPersistentCache::removeSSLSession(
  const std::string& hostname) noexcept {
  return persistentCache_->remove(hostname);
}

size_t SSLSessionPersistentCache::size() const {
  return persistentCache_->size();
}
}

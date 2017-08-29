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

#include <wangle/client/ssl/SSLSessionCacheUtils.h>

#include <folly/io/IOBuf.h>
#include <folly/Memory.h>
#include <folly/portability/OpenSSL.h>
#include <wangle/client/persistence/FilePersistentCache.h>

namespace wangle {

template<typename K>
SSLSessionPersistentCacheBase<K>::SSLSessionPersistentCacheBase(
    std::shared_ptr<PersistentCache<K, SSLSessionCacheData>> cache) :
  persistentCache_(cache),
  timeUtil_(new TimeUtil()) {}

template<typename K>
SSLSessionPersistentCacheBase<K>::SSLSessionPersistentCacheBase(
  const std::string& filename,
  const std::size_t cacheCapacity,
  const std::chrono::seconds& syncInterval) :
    SSLSessionPersistentCacheBase(
      std::make_shared<FilePersistentCache<K, SSLSessionCacheData>>(
        filename, cacheCapacity, syncInterval)) {}

template<typename K>
void SSLSessionPersistentCacheBase<K>::setSSLSession(
    const std::string& identity, SSLSessionPtr session) noexcept {
  if (!session) {
    return;
  }

  // We do not cache the session itself, but cache the session data from it in
  // order to recreate a new session later.
  auto sessionCacheData = getCacheDataForSession(session.get());
  if (sessionCacheData) {
    auto key = getKey(identity);
    sessionCacheData->addedTime = timeUtil_->now();
    persistentCache_->put(key, *sessionCacheData);
  }
}

template<typename K>
SSLSessionPtr SSLSessionPersistentCacheBase<K>::getSSLSession(
    const std::string& identity) const noexcept {
  auto key = getKey(identity);
  auto hit = persistentCache_->get(key);
  if (!hit) {
    return nullptr;
  }

  // Create a SSL_SESSION and return. In failure it returns nullptr.
  auto& value = hit.value();
  auto sess = SSLSessionPtr(getSessionFromCacheData(value));

#if OPENSSL_TICKETS
  if (sess &&
      SSL_SESSION_has_ticket(sess.get()) &&
      SSL_SESSION_get_ticket_lifetime_hint(sess.get()) > 0) {
    auto now = timeUtil_->now();
    auto secsBetween =
      std::chrono::duration_cast<std::chrono::seconds>(now - value.addedTime);
    if (secsBetween >= std::chrono::seconds(SSL_SESSION_get_ticket_lifetime_hint(sess.get()))) {
      return nullptr;
    }
  }
#endif

  return sess;
}

template<typename K>
bool SSLSessionPersistentCacheBase<K>::removeSSLSession(
  const std::string& identity) noexcept {
  auto key = getKey(identity);
  return persistentCache_->remove(key);
}

template<typename K>
size_t SSLSessionPersistentCacheBase<K>::size() const {
  return persistentCache_->size();
}

}

/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/client/ssl/ThreadSafeSSLSessionCache.h>

using folly::SharedMutex;

namespace wangle {

void ThreadSafeSSLSessionCache::setSSLSession(
  const std::string& identity,
  SSLSessionPtr session) noexcept {
  SharedMutex::WriteHolder lock(mutex_);
  delegate_->setSSLSession(identity, std::move(session));
}

SSLSessionPtr ThreadSafeSSLSessionCache::getSSLSession(
    const std::string& identity) const noexcept {
  SharedMutex::ReadHolder lock(mutex_);
  return delegate_->getSSLSession(identity);
}

bool ThreadSafeSSLSessionCache::removeSSLSession(
    const std::string& identity) noexcept {
  SharedMutex::WriteHolder lock(mutex_);
  return delegate_->removeSSLSession(identity);
}

bool ThreadSafeSSLSessionCache::supportsPersistence() const noexcept {
  SharedMutex::ReadHolder lock(mutex_);
  return delegate_->supportsPersistence();
}

size_t ThreadSafeSSLSessionCache::size() const {
  SharedMutex::ReadHolder lock(mutex_);
  return delegate_->size();
}

}

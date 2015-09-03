#include <wangle/client/ssl/ThreadSafeSSLSessionCache.h>

using folly::SharedMutex;

namespace wangle {

void ThreadSafeSSLSessionCache::setSSLSession(
  const std::string& hostname,
  SSLSessionPtr session) noexcept {
  SharedMutex::WriteHolder lock(mutex_);
  delegate_->setSSLSession(hostname, std::move(session));
}

SSLSessionPtr ThreadSafeSSLSessionCache::getSSLSession(
    const std::string& hostname) const noexcept {
  SharedMutex::ReadHolder lock(mutex_);
  return delegate_->getSSLSession(hostname);
}

bool ThreadSafeSSLSessionCache::removeSSLSession(
    const std::string& hostname) noexcept {
  SharedMutex::WriteHolder lock(mutex_);
  return delegate_->removeSSLSession(hostname);
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

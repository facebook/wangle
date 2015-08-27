#pragma once

#include <openssl/ssl.h>
#include <wangle/client/ssl/SSLSession.h>

namespace wangle {

/**
 * Callbacks related to SSL session cache
 *
 * This class contains three methods, setSSLSession() to store existing SSL
 * session data to cache, getSSLSession() to retreive cached session
 * data in cache, and removeSSLSession() to remove session data from cache.
 */
class SSLSessionCallbacks {
 public:
  virtual ~SSLSessionCallbacks() {}

  // Store the session data of the specified hostname in cache. Note that the
  // implementation must make it's own memory copy of the session data to put
  // into the cache.
  virtual void setSSLSession(
    const std::string& hostname, SSLSessionPtr session) noexcept = 0;

  // Return a SSL session if the cache contained session information for the
  // specified hostname. It is the caller's responsibility to decrement the
  // reference count of the returned session pointer.
  virtual SSLSessionPtr
  getSSLSession(const std::string& hostname) noexcept = 0;

  // Remove session data of the specified hostname from cache. Return true if
  // there was session data associated with the hostname before removal, or
  // false otherwise.
  virtual bool removeSSLSession(const std::string& hostname) noexcept = 0;

  // Return true if the underlying cache supports persistence
  virtual bool supportsPersistence() noexcept {
    return false;
  }

  virtual size_t size() const {
    return 0;
  }
};

}

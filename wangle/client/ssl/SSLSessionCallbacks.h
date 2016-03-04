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

#include <openssl/ssl.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <wangle/ssl/SSLUtil.h>
#include <wangle/client/ssl/SSLSession.h>
#include <wangle/client/ssl/SSLSessionCacheUtils.h>

#ifdef OPENSSL_NO_TLSEXT
#define OPENSSL_TICKETS 0
#else
#define OPENSSL_TICKETS \
    OPENSSL_VERSION_NUMBER >= 0x1000105fL
#endif

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
  // Store the session data of the specified identity in cache. Note that the
  // implementation must make it's own memory copy of the session data to put
  // into the cache.
  virtual void setSSLSession(
    const std::string& identity, SSLSessionPtr session) noexcept = 0;

  // Return a SSL session if the cache contained session information for the
  // specified identity. It is the caller's responsibility to decrement the
  // reference count of the returned session pointer.
  virtual SSLSessionPtr getSSLSession(
    const std::string& identity) const noexcept = 0;

  // Remove session data of the specified identity from cache. Return true if
  // there was session data associated with the identity before removal, or
  // false otherwise.
  virtual bool removeSSLSession(const std::string& identity) noexcept = 0;

  // Return true if the underlying cache supports persistence
  virtual bool supportsPersistence() const noexcept {
    return false;
  }

  virtual size_t size() const {
    return 0;
  }

  virtual ~SSLSessionCallbacks() {}

  /**
   * Sets up SSL Session callbacks on a context.  The application is
   * responsible for detaching the callbacks from the context.
   */
  static void attachCallbacksToContext(SSL_CTX* ctx,
                                       SSLSessionCallbacks* callbacks) {
    SSL_CTX_set_session_cache_mode(ctx,
        SSL_SESS_CACHE_NO_INTERNAL |
        SSL_SESS_CACHE_CLIENT |
        SSL_SESS_CACHE_NO_AUTO_CLEAR);
    // Only initializes the cache index the first time.
    SSLUtil::getSSLCtxExIndex(&getCacheIndex());
    SSL_CTX_set_ex_data(ctx, getCacheIndex(), callbacks);
    SSL_CTX_sess_set_new_cb(ctx, SSLSessionCallbacks::newSessionCallback);
    SSL_CTX_sess_set_remove_cb(
      ctx, SSLSessionCallbacks::removeSessionCallback);
  }

  /**
   * Detach the passed in callbacks from the context.  If the callbacks are not
   * set on the context, it is unchanged.
   */
  static void detachCallbacksFromContext(SSL_CTX* ctx,
                                         SSLSessionCallbacks* callbacks) {
    auto sslSessionCache = getCacheFromContext(ctx);
    if (sslSessionCache != callbacks) {
      return;
    }
    // We don't unset flags here because we cannot assume that we are the only
    // code that sets the cache flags.
    SSL_CTX_set_ex_data(ctx, getCacheIndex(), nullptr);
    SSL_CTX_sess_set_new_cb(ctx, nullptr);
    SSL_CTX_sess_set_remove_cb(ctx, nullptr);
  }

  static SSLSessionCallbacks* getCacheFromContext(SSL_CTX* ctx) {
    return static_cast<SSLSessionCallbacks*>(
      SSL_CTX_get_ex_data(ctx, getCacheIndex()));
  }

 private:

  static std::string getServiceIdentityFromSSL(SSL* ssl) {
    auto sock = folly::AsyncSSLSocket::getFromSSL(ssl);
    return sock ? sock->getServiceIdentity() : "";
  }

  static int newSessionCallback(SSL* ssl, SSL_SESSION* session) {
    SSLSessionPtr sessionPtr(session);
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
    auto sslSessionCache = getCacheFromContext(ctx);
    std::string identity = getServiceIdentityFromSSL(ssl);
    if (identity.empty()) {
      const char* name = folly::AsyncSSLSocket::getSSLServerNameFromSSL(ssl);
      identity = name ? name : "";
    }
    if (!identity.empty()) {
      setSessionServiceIdentity(session, identity);
      sslSessionCache->setSSLSession(identity, std::move(sessionPtr));
      return 1;
    }
    return -1;
  }

  static void removeSessionCallback(SSL_CTX* ctx, SSL_SESSION* session) {
    auto sslSessionCache = getCacheFromContext(ctx);
    auto identity = getSessionServiceIdentity(session);
    if (identity && !identity->empty()) {
      sslSessionCache->removeSSLSession(*identity);
    }
#if OPENSSL_TICKETS
    else {
      if (session->tlsext_hostname) {
        auto hostname = std::string(session->tlsext_hostname);
        sslSessionCache->removeSSLSession(hostname);
      }
    }
#endif
  }

  static int32_t& getCacheIndex() {
    static int32_t sExDataIndex = -1;
    return sExDataIndex;
  }

};

}

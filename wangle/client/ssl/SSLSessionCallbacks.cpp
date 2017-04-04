/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
//
#include <wangle/client/ssl/SSLSessionCallbacks.h>

using namespace std::chrono;
using namespace folly::ssl;

namespace wangle {
// static
void SSLSessionCallbacks::attachCallbacksToContext(
    SSL_CTX* ctx,
    SSLSessionCallbacks* callbacks) {
  SSL_CTX_set_session_cache_mode(
      ctx,
      SSL_SESS_CACHE_NO_INTERNAL | SSL_SESS_CACHE_CLIENT |
          SSL_SESS_CACHE_NO_AUTO_CLEAR);
  // Only initializes the cache index the first time.
  SSLUtil::getSSLCtxExIndex(&getCacheIndex());
  SSL_CTX_set_ex_data(ctx, getCacheIndex(), callbacks);
  SSL_CTX_sess_set_new_cb(ctx, SSLSessionCallbacks::newSessionCallback);
  SSL_CTX_sess_set_remove_cb(ctx, SSLSessionCallbacks::removeSessionCallback);
}

// static
void SSLSessionCallbacks::detachCallbacksFromContext(
    SSL_CTX* ctx,
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

// static
SSLSessionCallbacks* SSLSessionCallbacks::getCacheFromContext(SSL_CTX* ctx) {
  return static_cast<SSLSessionCallbacks*>(
      SSL_CTX_get_ex_data(ctx, getCacheIndex()));
}

// static
std::string SSLSessionCallbacks::getServiceIdentityFromSSL(SSL* ssl) {
  auto sock = folly::AsyncSSLSocket::getFromSSL(ssl);
  return sock ? sock->getServiceIdentity() : "";
}

// static
int SSLSessionCallbacks::newSessionCallback(SSL* ssl, SSL_SESSION* session) {
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

// static
void SSLSessionCallbacks::removeSessionCallback(
    SSL_CTX* ctx,
    SSL_SESSION* session) {
  auto sslSessionCache = getCacheFromContext(ctx);
  auto identity = getSessionServiceIdentity(session);
  if (identity && !identity->empty()) {
    sslSessionCache->removeSSLSession(*identity);
  }
#if OPENSSL_TICKETS
  else {
    auto hostname = SSL_SESSION_get0_hostname(session);
    if (hostname) {
      sslSessionCache->removeSSLSession(std::string(hostname));
    }
  }
#endif
}
} // wangle

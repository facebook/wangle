#pragma once

#include <openssl/ssl.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <wangle/ssl/SSLUtil.h>
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
  // Store the session data of the specified hostname in cache. Note that the
  // implementation must make it's own memory copy of the session data to put
  // into the cache.
  virtual void setSSLSession(
    const std::string& hostname, SSLSessionPtr session) noexcept = 0;

  // Return a SSL session if the cache contained session information for the
  // specified hostname. It is the caller's responsibility to decrement the
  // reference count of the returned session pointer.
  virtual SSLSessionPtr
  getSSLSession(const std::string& hostname) const noexcept = 0;

  // Remove session data of the specified hostname from cache. Return true if
  // there was session data associated with the hostname before removal, or
  // false otherwise.
  virtual bool removeSSLSession(const std::string& hostname) noexcept = 0;

  // Return true if the underlying cache supports persistence
  virtual bool supportsPersistence() const noexcept {
    return false;
  }

  virtual size_t size() const {
    return 0;
  }

  /**
   * Configures the caching callbacks to set and remove new sessions on
   * the SSLContext during handshakes.
   * Only one session must be configured at one time on an SSLContext.
   * If this session callbacks is attached to another SSLContext, we detach
   * the SessionCallbacks from that SSLContext and attach it to the new
   * SSLContext.
   * Due to a limitation of the OpenSSL API, to use resumption correctly, a
   * client must call getSSLSession() to get a session for the socket.
   */
  void attachToContext(folly::SSLContextPtr sslContext) {
    // If there was a previous context attached, detach it.
    detachFromContext();

    sslCtx_ = sslContext;
    SSL_CTX* ctx = sslContext->getSSLCtx();
    SSL_CTX_set_session_cache_mode(ctx,
        SSL_SESS_CACHE_NO_INTERNAL |
        SSL_SESS_CACHE_CLIENT |
        SSL_SESS_CACHE_NO_AUTO_CLEAR);
    // Only initializes the cache index the first time.
    SSLUtil::getSSLCtxExIndex(&getCacheIndex());
    SSL_CTX_set_ex_data(ctx, getCacheIndex(), this);
    SSL_CTX_sess_set_new_cb(ctx, SSLSessionCallbacks::newSessionCallback);
    SSL_CTX_sess_set_remove_cb(ctx, SSLSessionCallbacks::removeSessionCallback);
  }

  /**
   * Detaches the SSLSessionCallbacks from a SSLContext if it was attached.
   */
  void detachFromContext() {
    if (sslCtx_) {
      SSL_CTX* ctx = sslCtx_->getSSLCtx();
      // We don't unset flags here because we cannot assume that we are the only
      // code that sets the cache flags.
      SSL_CTX_set_ex_data(ctx, getCacheIndex(), nullptr);
      SSL_CTX_sess_set_new_cb(ctx, nullptr);
      SSL_CTX_sess_set_remove_cb(ctx, nullptr);
      sslCtx_ = nullptr;
    }
  }

  virtual ~SSLSessionCallbacks() {
    detachFromContext();
  }

 private:
  static int newSessionCallback(SSL* ssl, SSL_SESSION* session) {
    SSLSessionPtr sessionPtr(session);
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
    SSLSessionCallbacks* sslSessionCache =
      (SSLSessionCallbacks*) SSL_CTX_get_ex_data(ctx, getCacheIndex());
    folly::AsyncSSLSocket *sslSocket = folly::AsyncSSLSocket::getFromSSL(ssl);
    if (sslSocket) {
      const char* serverName = sslSocket->getSSLServerNameNoThrow();
      if (serverName) {
        sslSessionCache->setSSLSession(
            std::string(serverName),
            std::move(sessionPtr));
        return 1;
      }
    }
    return -1;
  }

  static void removeSessionCallback(SSL_CTX* ctx, SSL_SESSION* session) {
    SSLSessionCallbacks* sslSessionCache =
      (SSLSessionCallbacks*) SSL_CTX_get_ex_data(ctx, getCacheIndex());
    if (session->tlsext_hostname) {
      sslSessionCache->removeSSLSession(std::string(session->tlsext_hostname));
    }
  }

  static int32_t& getCacheIndex() {
    static int32_t sExDataIndex = -1;
    return sExDataIndex;
  }

  folly::SSLContextPtr sslCtx_;
};

}

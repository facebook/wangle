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

#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <folly/SharedMutex.h>

#include <glog/logging.h>
#include <list>
#include <memory>
#include <wangle/ssl/SSLContextConfig.h>
#include <wangle/ssl/SSLSessionCacheManager.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>
#include <wangle/acceptor/SSLContextSelectionMisc.h>
#include <vector>

namespace folly {

class SocketAddress;
class SSLContext;

}

namespace wangle {

class ClientHelloExtStats;
struct SSLCacheOptions;
class SSLStats;
class TLSTicketKeyManager;
struct TLSTicketKeySeeds;
class ServerSSLContext;

class SSLContextManager {
 private:
  struct SslContexts {
    void clear();
    void swap(SslContexts& other) noexcept;

    std::vector<std::shared_ptr<ServerSSLContext>> ctxs;
    std::shared_ptr<ServerSSLContext> defaultCtx;
    std::string defaultCtxDomainName;

    /**
     * Container to store the (DomainName -> SSL_CTX) mapping
     */
    std::unordered_map<
      SSLContextKey,
      std::shared_ptr<folly::SSLContext>,
      SSLContextKeyHash> dnMap;
  };

 public:

   /**
   * Provide ability to perform explicit client certificate
   * verification
   */
   struct ClientCertVerifyCallback {

     // no-op. Should be overridden if actual
     // verification is required
     virtual void attachSSLContext(
       const std::shared_ptr<folly::SSLContext>& sslCtx) = 0;
     virtual ~ClientCertVerifyCallback() {}
   };


  explicit SSLContextManager(folly::EventBase* eventBase,
                             const std::string& vipName, bool strict,
                             SSLStats* stats);
  virtual ~SSLContextManager();

  /**
   * Add a new X509 to SSLContextManager.  The details of a X509
   * is passed as a SSLContextConfig object.
   *
   * @param ctxConfig     Details of a X509, its private key, password, etc.
   * @param cacheOptions  Options for how to do session caching.
   * @param ticketSeeds   If non-null, the initial ticket key seeds to use.
   * @param vipAddress    Which VIP are the X509(s) used for? It is only for
   *                      for user friendly log message
   * @param externalCache Optional external provider for the session cache;
   *                      may be null
   */
  void addSSLContextConfig(
    const SSLContextConfig& ctxConfig,
    const SSLCacheOptions& cacheOptions,
    const TLSTicketKeySeeds* ticketSeeds,
    const folly::SocketAddress& vipAddress,
    const std::shared_ptr<SSLCacheProvider> &externalCache,
    SslContexts* contexts = nullptr);

  /**
   * Resets SSLContextManager with new X509s
   *
   * @param ctxConfigs    Details of a X509s, private key, password, etc.
   * @param cacheOptions  Options for how to do session caching.
   * @param ticketSeeds   If non-null, the initial ticket key seeds to use.
   * @param vipAddress    Which VIP are the X509(s) used for? It is only for
   *                      for user friendly log message
   * @param externalCache Optional external provider for the session cache;
   *                      may be null
   */
  void resetSSLContextConfigs(
    const std::vector<SSLContextConfig>& ctxConfig,
    const SSLCacheOptions& cacheOptions,
    const TLSTicketKeySeeds* ticketSeeds,
    const folly::SocketAddress& vipAddress,
    const std::shared_ptr<SSLCacheProvider> &externalCache);

  /**
   * Clears all ssl contexts
   */
  void clear();

  /**
   * Get the default SSL_CTX for a VIP
   */
  std::shared_ptr<folly::SSLContext>
    getDefaultSSLCtx() const;

  /**
   * Search first by exact domain, then by one level up
   */
  std::shared_ptr<folly::SSLContext>
    getSSLCtx(const SSLContextKey& key) const;

  /**
   * Search by the _one_ level up subdomain
   */
  std::shared_ptr<folly::SSLContext>
    getSSLCtxBySuffix(const SSLContextKey& key) const;

  /**
   * Search by the full-string domain name
   */
  std::shared_ptr<folly::SSLContext>
    getSSLCtxByExactDomain(const SSLContextKey& key) const;

  void reloadTLSTicketKeys(const std::vector<std::string>& oldSeeds,
                           const std::vector<std::string>& currentSeeds,
                           const std::vector<std::string>& newSeeds);

  void setSSLStats(SSLStats* stats) {
    stats_ = stats;
  }

  /**
   * SSLContextManager only collects SNI stats now
   */
  void setClientHelloExtStats(ClientHelloExtStats* stats) {
    clientHelloTLSExtStats_ = stats;
  }

  void setClientVerifyCallback(std::unique_ptr<ClientCertVerifyCallback> cb) {
        clientCertVerifyCallback_ = std::move(cb);
  }

 protected:
  virtual void enableAsyncCrypto(
    const std::shared_ptr<folly::SSLContext>&,
    const SSLContextConfig&,
    const std::string& /* certificateFile */) {
    LOG(FATAL) << "Unsupported in base SSLContextManager";
  }

  virtual void loadCertificate(
      wangle::ServerSSLContext* sslCtx,
      const SSLContextConfig& ctxConfig,
      const std::string& certPath);

  virtual void overrideConfiguration(
    const std::shared_ptr<folly::SSLContext>&,
    const SSLContextConfig&) {}

  std::string vipName_;
  SSLStats* stats_{nullptr};

  /**
   * Insert a SSLContext by domain name.
   */
  void insertSSLCtxByDomainName(
    const char* dn,
    size_t len,
    std::shared_ptr<folly::SSLContext> sslCtx,
    SslContexts& contexts,
    CertCrypto certCrypto = CertCrypto::BEST_AVAILABLE);

  void insertSSLCtxByDomainName(
    const char* dn,
    size_t len,
    std::shared_ptr<folly::SSLContext> sslCtx,
    CertCrypto certCrypto = CertCrypto::BEST_AVAILABLE) {
    insertSSLCtxByDomainName(dn, len, sslCtx, contexts_, certCrypto);
  }

 private:
  SSLContextManager(const SSLContextManager&) = delete;

  void ctxSetupByOpensslFeature(
    std::shared_ptr<ServerSSLContext> sslCtx,
    const SSLContextConfig& ctxConfig,
    SslContexts& contexts);

  /**
   * Callback function from openssl to find the right X509 to
   * use during SSL handshake
   */
#if FOLLY_OPENSSL_HAS_SNI
# define PROXYGEN_HAVE_SERVERNAMECALLBACK
  folly::SSLContext::ServerNameCallbackResult
    serverNameCallback(SSL* ssl);
#endif

  /**
   * The following functions help to maintain the data structure for
   * domain name matching in SNI.  Some notes:
   *
   * 1. It is a best match.
   *
   * 2. It allows wildcard CN and wildcard subject alternative name in a X509.
   *    The wildcard name must be _prefixed_ by '*.'.  It errors out whenever
   *    it sees '*' in any other locations.
   *
   * 3. It uses one std::unordered_map<DomainName, SSL_CTX> object to
   *    do this.  For wildcard name like "*.facebook.com", ".facebook.com"
   *    is used as the key.
   *
   * 4. After getting tlsext_hostname from the client hello message, it
   *    will do a full string search first and then try one level up to
   *    match any wildcard name (if any) in the X509.
   *    [Note, browser also only looks one level up when matching the requesting
   *     domain name with the wildcard name in the server X509].
   */

  void insert(
    std::shared_ptr<ServerSSLContext> sslCtx,
    bool defaultFallback,
    SslContexts& contexts);

  void insertSSLCtxByDomainNameImpl(
    const char* dn,
    size_t len,
    std::shared_ptr<folly::SSLContext> sslCtx,
    SslContexts& contexts,
    CertCrypto certCrypto);

  void insertIntoDnMap(SSLContextKey key,
    std::shared_ptr<folly::SSLContext> sslCtx,
    bool overwrite,
    SslContexts& contexts);

  SslContexts contexts_;
  folly::EventBase* eventBase_;
  ClientHelloExtStats* clientHelloTLSExtStats_{nullptr};
  SSLContextConfig::SNINoMatchFn noMatchFn_;
  bool strict_{true};
  std::unique_ptr<ClientCertVerifyCallback> clientCertVerifyCallback_{nullptr};
};

} // namespace wangle

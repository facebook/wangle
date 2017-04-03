/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/ssl/ServerSSLContext.h>

#include <folly/Memory.h>
#include <wangle/ssl/SSLCacheOptions.h>
#include <wangle/ssl/SSLContextConfig.h>
#include <wangle/ssl/SSLSessionCacheManager.h>
#include <wangle/ssl/TLSTicketKeyManager.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

using folly::SSLContext;
using folly::EventBase;

namespace wangle {

void ServerSSLContext::setupTicketManager(
    const TLSTicketKeySeeds* ticketSeeds,
    const SSLContextConfig& ctxConfig,
    SSLStats* stats) {
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  if (ticketSeeds && ctxConfig.sessionTicketEnabled) {
    ticketManager_ = folly::make_unique<TLSTicketKeyManager>(this, stats);
    ticketManager_->setTLSTicketKeySeeds(
        ticketSeeds->oldSeeds,
        ticketSeeds->currentSeeds,
        ticketSeeds->newSeeds);
  } else {
    setOptions(SSL_OP_NO_TICKET);
    ticketManager_.reset();
  }
#else
  if (ticketSeeds && ctxConfig.sessionTicketEnabled) {
    OPENSSL_MISSING_FEATURE(TLSTicket);
  }
#endif
}

void ServerSSLContext::setupSessionCache(
    const SSLContextConfig& ctxConfig,
    const SSLCacheOptions& cacheOptions,
    const folly::SocketAddress& vipAddress,
    const std::shared_ptr<SSLCacheProvider>& externalCache,
    const std::string& commonName,
    folly::EventBase* evb,
    SSLStats* stats) {
  // the internal cache never does what we want (per-thread-per-vip).
  // Disable it.  SSLSessionCacheManager will set it appropriately.
  SSL_CTX_set_session_cache_mode(getSSLCtx(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_timeout(getSSLCtx(), cacheOptions.sslCacheTimeout.count());
  std::string sessionContext;
  if (ctxConfig.sessionContext) {
    sessionContext = *ctxConfig.sessionContext;
  } else {
    sessionContext = commonName;
  }
  if (ctxConfig.sessionCacheEnabled &&
      cacheOptions.maxSSLCacheSize > 0 &&
      cacheOptions.sslCacheFlushSize > 0) {
    sessionCacheManager_ = folly::make_unique<SSLSessionCacheManager>(
      cacheOptions.maxSSLCacheSize,
      cacheOptions.sslCacheFlushSize,
      this,
      vipAddress,
      sessionContext,
      evb,
      stats,
      externalCache);
  } else {
    sessionCacheManager_.reset();
  }
  // even though SSLSessionCacheManager might set the context if enabled,
  // we also want to setup the context in case a cache is not enabled.
  setSessionCacheContext(sessionContext);
}

}

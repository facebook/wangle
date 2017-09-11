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

ServerSSLContext::ServerSSLContext(SSLVersion version)
    : folly::SSLContext(version) {
  setSessionCacheContext("ServerSSLContext");
}

void ServerSSLContext::setupTicketManager(
    const TLSTicketKeySeeds* ticketSeeds,
    const SSLContextConfig& ctxConfig,
    SSLStats* stats) {
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  if (ticketSeeds && ctxConfig.sessionTicketEnabled) {
    ticketManager_ = std::make_unique<TLSTicketKeyManager>(this, stats);
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
    const std::shared_ptr<SSLCacheProvider>& externalCache,
    const std::string& sessionIdContext,
    SSLStats* stats) {
  // the internal cache never does what we want (per-thread-per-vip).
  // Disable it.  SSLSessionCacheManager will set it appropriately.
  SSL_CTX_set_session_cache_mode(getSSLCtx(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_timeout(getSSLCtx(), cacheOptions.sslCacheTimeout.count());
  if (ctxConfig.sessionCacheEnabled &&
      cacheOptions.maxSSLCacheSize > 0 &&
      cacheOptions.sslCacheFlushSize > 0) {
    sessionCacheManager_ = std::make_unique<SSLSessionCacheManager>(
      cacheOptions.maxSSLCacheSize,
      cacheOptions.sslCacheFlushSize,
      this,
      sessionIdContext,
      stats,
      externalCache);
  } else {
    sessionCacheManager_.reset();
  }
}

}

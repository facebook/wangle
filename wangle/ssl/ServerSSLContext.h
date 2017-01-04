/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>

#include <folly/io/async/SSLContext.h>

namespace folly {

class EventBase;
class SocketAddress;

}

namespace wangle {

struct SSLCacheOptions;
struct SSLContextConfig;
class SSLStats;
class TLSTicketKeyManager;
struct TLSTicketKeySeeds;
class SSLSessionCacheManager;
class SSLCacheProvider;

// A SSL Context that owns a session cache and ticket key manager.
// It is used for server side SSL connections.
class ServerSSLContext : public folly::SSLContext {
 public:
  using folly::SSLContext::SSLContext;

  virtual ~ServerSSLContext() = default;

  void setupTicketManager(
      const TLSTicketKeySeeds* ticketSeeds,
      const SSLContextConfig& ctxConfig,
      SSLStats* stats);

  void setupSessionCache(
      const SSLContextConfig& ctxConfig,
      const SSLCacheOptions& cacheOptions,
      const folly::SocketAddress& vipAddress,
      const std::shared_ptr<SSLCacheProvider>& externalCache,
      const std::string& commonName,
      folly::EventBase* evb,
      SSLStats* stats);

  // Get the ticket key manager that this context manages.
  TLSTicketKeyManager* getTicketManager() {
    return ticketManager_.get();
  }

  // Get the session cache manager that this context manages.
  SSLSessionCacheManager* getSessionCacheManager() {
    return sessionCacheManager_.get();
  }

 private:
  std::unique_ptr<TLSTicketKeyManager> ticketManager_;
  std::unique_ptr<SSLSessionCacheManager> sessionCacheManager_;
};

}

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

#include <wangle/acceptor/PeekingAcceptorHandshakeHelper.h>
#include <wangle/acceptor/SSLAcceptorHandshakeHelper.h>
#include <wangle/acceptor/UnencryptedAcceptorHandshakeHelper.h>

namespace wangle {

/**
 * A handshake manager that makes it convenient to create a server
 * that will accept both TLS and plaintext traffic.
 */
class TLSPlaintextHandshakeManager : public AcceptorHandshakeManager {
 public:
  using AcceptorHandshakeManager::AcceptorHandshakeManager;

private:
  enum { kPeekCount = 9 };
  using PeekingHelper = PeekingAcceptorHandshakeHelper<kPeekCount>;

  class PeekingCallback : public PeekingHelper::PeekCallback {
    public:
      virtual AcceptorHandshakeHelper::UniquePtr getHelper(
          std::array<uint8_t, kPeekCount> bytes,
          Acceptor* acceptor,
          const folly::SocketAddress& clientAddr,
          std::chrono::steady_clock::time_point acceptTime,
          TransportInfo& tinfo) override;
  };

 protected:
  virtual void startHelper(folly::AsyncSSLSocket::UniquePtr sock) override {
    helper_.reset(new PeekingAcceptorHandshakeHelper<kPeekCount>(
        acceptor_, clientAddr_, acceptTime_, tinfo_, &peekCallback_));
    helper_->start(std::move(sock), this);
  }

 private:
  static bool looksLikeTLS(const std::array<uint8_t, kPeekCount>& peekBytes);

  PeekingCallback peekCallback_;
};
}

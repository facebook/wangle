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

#include <wangle/acceptor/PeekingAcceptorHandshakeHelper.h>
#include <wangle/acceptor/SSLAcceptorHandshakeHelper.h>
#include <wangle/acceptor/UnencryptedAcceptorHandshakeHelper.h>

namespace wangle {

/**
 * A peeking callback that makes it convenient to create a server
 * that will accept both TLS and plaintext traffic.
 */
class TLSPlaintextPeekingCallback :
  public PeekingAcceptorHandshakeHelper::PeekCallback {
  enum { kPeekCount = 9 };
 public:
  TLSPlaintextPeekingCallback():
    PeekingAcceptorHandshakeHelper::PeekCallback(kPeekCount) {}

  AcceptorHandshakeHelper::UniquePtr getHelper(
      const std::vector<uint8_t>& bytes,
      Acceptor* acceptor,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo& tinfo) override;

 private:
  static bool looksLikeTLS(const std::vector<uint8_t>& peekBytes);
};

}

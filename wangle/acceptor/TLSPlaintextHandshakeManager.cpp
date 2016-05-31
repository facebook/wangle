/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/TLSPlaintextHandshakeManager.h>

namespace wangle {

bool TLSPlaintextHandshakeManager::looksLikeTLS(
    const std::array<uint8_t, kPeekCount>& bytes) {
  // TLS starts with
  // 0: 0x16 - handshake magic
  // 1: 0x03 - SSL major version
  // 2: 0x00 to 0x03 - minor version
  // 3-4: Length
  // 4: 0x01 - Handshake type (Client Hello)
  if (bytes[0] != 0x16 || bytes[1] != 0x03 || bytes[5] != 0x01) {
    return false;
  }
  return true;
}

AcceptorHandshakeHelper::UniquePtr
TLSPlaintextHandshakeManager::PeekingCallback::getHelper(
    std::array<uint8_t, kPeekCount> bytes,
    Acceptor* acceptor,
    const folly::SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    TransportInfo& tinfo) {
  if (TLSPlaintextHandshakeManager::looksLikeTLS(bytes)) {
    return AcceptorHandshakeHelper::UniquePtr(new SSLAcceptorHandshakeHelper(
        acceptor, clientAddr, acceptTime, tinfo));
  }
  return AcceptorHandshakeHelper::UniquePtr(
      new UnencryptedAcceptorHandshakeHelper());
}
}

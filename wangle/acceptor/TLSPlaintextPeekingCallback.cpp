/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/TLSPlaintextPeekingCallback.h>

namespace wangle {

bool TLSPlaintextPeekingCallback::looksLikeTLS(
    const std::vector<uint8_t>& bytes) {
  CHECK_GE(bytes.size(), kPeekCount);
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
TLSPlaintextPeekingCallback::getHelper(
    const std::vector<uint8_t>& bytes,
    Acceptor*,
    const folly::SocketAddress& /* clientAddr */,
    std::chrono::steady_clock::time_point /* acceptTime */,
    TransportInfo&) {
  if (!TLSPlaintextPeekingCallback::looksLikeTLS(bytes)) {
    return AcceptorHandshakeHelper::UniquePtr(
        new UnencryptedAcceptorHandshakeHelper());
  }

  return nullptr;
}

}

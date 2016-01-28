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

#include <wangle/acceptor/AcceptorHandshakeManager.h>

namespace wangle {

/**
 * This is a dummy handshake helper that immediately returns the socket to the
 * acceptor. This can be used with the peeking acceptor if no handshake is
 * needed.
 */
class UnencryptedAcceptorHandshakeHelper : public AcceptorHandshakeHelper {
 public:
  UnencryptedAcceptorHandshakeHelper() = default;

  virtual void start(
      folly::AsyncSSLSocket::UniquePtr sock,
      AcceptorHandshakeHelper::Callback* callback) noexcept override {
    callback->connectionReady(
      std::move(sock),
      "",
      SecureTransportType::NONE);
  }

  virtual void dropConnection(
      SSLErrorEnum reason = SSLErrorEnum::NO_ERROR) override {
    CHECK(false) << "Nothing to drop";
  }
};

}

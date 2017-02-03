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

namespace wangle {

/**
 * This class holds different peekers that will be used to get the appropriate
 * AcceptorHandshakeHelper to handle the security protocol negotiation.
 */
class SecurityProtocolContextManager {
 public:
  /**
   * Adds a peeker to be used when accepting connections on a secure port.
   * Peekers will be used in the order they are added.
   */
  void addPeeker(PeekingCallbackPtr peekingCallback) {
    if (peekingCallback->getBytesRequired() > numBytes_) {
      numBytes_ = peekingCallback->getBytesRequired();
    }
    peekingCallbacks_.push_back(std::move(peekingCallback));
  }

  AcceptorHandshakeManager* getHandshakeManager(
      Acceptor* acceptor,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo& tinfo) noexcept {
    return new PeekingAcceptorHandshakeManager(
        acceptor, clientAddr, acceptTime, tinfo, peekingCallbacks_, numBytes_);
  }

  size_t getPeekBytes() const {
    return numBytes_;
  }

 private:
  /**
   * Peeking callbacks for each handshake protocol.
   */
  std::vector<PeekingCallbackPtr> peekingCallbacks_;

  /**
   * Highest number of bytes required by a peeking callback.
   */
  size_t numBytes_{0};
};

}

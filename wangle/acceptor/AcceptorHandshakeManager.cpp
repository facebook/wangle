/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/acceptor/AcceptorHandshakeManager.h>
#include <wangle/acceptor/Acceptor.h>

namespace wangle {

void AcceptorHandshakeManager::start(
    folly::AsyncSSLSocket::UniquePtr sock) noexcept {
  acceptor_->getConnectionManager()->addConnection(this, true);
  startHelper(std::move(sock));
  startHandshakeTimeout();
}

void AcceptorHandshakeManager::connectionReady(
      folly::AsyncTransportWrapper::UniquePtr transport,
      std::string nextProtocol,
      SecureTransportType secureTransportType,
      folly::Optional<SSLErrorEnum> sslErr) noexcept {
  if (sslErr) {
    acceptor_->updateSSLStats(
        transport.get(),
        timeSinceAcceptMs(),
        sslErr.value());
  }
  acceptor_->getConnectionManager()->removeConnection(this);
  // We pass TransportInfo by reference even though we're about to destroy it,
  // so lets hope that anything saving it makes a copy!
  acceptor_->sslConnectionReady(
      std::move(transport),
      std::move(clientAddr_),
      std::move(nextProtocol),
      secureTransportType,
      tinfo_);
  destroy();
}

void AcceptorHandshakeManager::connectionError(
    folly::AsyncTransportWrapper* transport,
    folly::exception_wrapper ex,
    folly::Optional<SSLErrorEnum> sslErr) noexcept {
  if (sslErr) {
    acceptor_->updateSSLStats(
        transport, timeSinceAcceptMs(), sslErr.value());
  }
  acceptor_->getConnectionManager()->removeConnection(this);
  acceptor_->sslConnectionError(std::move(ex));
  destroy();
}

std::chrono::milliseconds AcceptorHandshakeManager::timeSinceAcceptMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - acceptTime_);
}

void AcceptorHandshakeManager::startHandshakeTimeout() {
  auto handshake_timeout = acceptor_->getSSLHandshakeTimeout();
  acceptor_->getConnectionManager()->scheduleTimeout(
      this, handshake_timeout);
}

}

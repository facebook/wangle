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

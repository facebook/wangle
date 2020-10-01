/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <chrono>
#include <folly/ExceptionWrapper.h>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/acceptor/SecureTransportType.h>
#include <wangle/acceptor/TransportInfo.h>

namespace wangle {

/**
 * Helper to return a string of the given transport's client IP and port.
 */
std::string describeAddresses(const folly::AsyncTransport* transport);

class Acceptor;

class AcceptorHandshakeHelper : public folly::DelayedDestruction {
 public:
  using UniquePtr = std::unique_ptr<
    AcceptorHandshakeHelper, folly::DelayedDestruction::Destructor>;

  class Callback {
   public:
    virtual ~Callback() = default;

    /**
     * Called after handshake has been completed successfully.
     *
     * If sslErr is set, Acceptor::updateSSLStats will be called.
     */
    virtual void connectionReady(
        folly::AsyncTransport::UniquePtr transport,
        std::string nextProtocol,
        SecureTransportType secureTransportType,
        folly::Optional<SSLErrorEnum> sslErr) noexcept = 0;

    /**
     * Called if an error was encountered while performing handshake.
     *
     * If sslErr is set, Acceptor::updateSSLStats will be called.
     */
    virtual void connectionError(
        folly::AsyncTransport* transport,
        folly::exception_wrapper ex,
        folly::Optional<SSLErrorEnum> sslErr) noexcept = 0;
  };

  virtual void start(
      folly::AsyncSSLSocket::UniquePtr sock,
      AcceptorHandshakeHelper::Callback* callback) noexcept = 0;

  virtual void dropConnection(SSLErrorEnum reason = SSLErrorEnum::NO_ERROR) = 0;
};

class AcceptorHandshakeManager : public ManagedConnection,
                                 public AcceptorHandshakeHelper::Callback {
 public:
  AcceptorHandshakeManager(
      Acceptor* acceptor,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo tinfo) :
    acceptor_(acceptor),
    clientAddr_(clientAddr),
    acceptTime_(acceptTime),
    tinfo_(std::move(tinfo)) {}

  ~AcceptorHandshakeManager() override = default;

  virtual void start(folly::AsyncSSLSocket::UniquePtr sock) noexcept;

  void timeoutExpired() noexcept override {
    VLOG(4) << "SSL handshake timeout expired";
    dropConnection(SSLErrorEnum::TIMEOUT);
  }

  void describe(std::ostream& os) const override {
    os << "pending handshake on " << clientAddr_;
  }

  bool isBusy() const override {
    return true;
  }

  void notifyPendingShutdown() override {}

  void closeWhenIdle() override {}

  void dropConnection(const std::string& /* errorMsg */ = "") override {
    dropConnection(SSLErrorEnum::NO_ERROR);
  }

  void dropConnection(SSLErrorEnum reason) {
    VLOG(10) << "Dropping in progress handshake for " << clientAddr_;
    helper_->dropConnection(reason);
  }

  void dumpConnectionState(uint8_t /* loglevel */) override {}

 protected:
  void connectionReady(
      folly::AsyncTransport::UniquePtr transport,
      std::string nextProtocol,
      SecureTransportType secureTransportType,
      folly::Optional<SSLErrorEnum>
          details) noexcept override;

  void connectionError(
      folly::AsyncTransport* transport,
      folly::exception_wrapper ex,
      folly::Optional<SSLErrorEnum>
          details) noexcept override;

  std::chrono::milliseconds timeSinceAcceptMs() const;

  virtual void startHelper(folly::AsyncSSLSocket::UniquePtr sock) = 0;

  void startHandshakeTimeout();

  Acceptor* acceptor_;
  folly::SocketAddress clientAddr_;
  std::chrono::steady_clock::time_point acceptTime_;
  TransportInfo tinfo_;
  AcceptorHandshakeHelper::UniquePtr helper_;
};

}

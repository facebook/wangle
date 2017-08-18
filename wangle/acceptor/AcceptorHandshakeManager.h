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

#include <chrono>
#include <folly/ExceptionWrapper.h>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSocket.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/acceptor/SecureTransportType.h>
#include <wangle/acceptor/TransportInfo.h>

namespace wangle {

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
        folly::AsyncTransportWrapper::UniquePtr transport,
        std::string nextProtocol,
        SecureTransportType secureTransportType,
        folly::Optional<SSLErrorEnum> sslErr) noexcept = 0;

    /**
     * Called if an error was encountered while performing handshake.
     *
     * If sslErr is set, Acceptor::updateSSLStats will be called.
     */
    virtual void connectionError(
        folly::AsyncTransportWrapper* transport,
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

  void dropConnection() override {
    dropConnection(SSLErrorEnum::NO_ERROR);
  }

  void dropConnection(SSLErrorEnum reason) {
    VLOG(10) << "Dropping in progress handshake for " << clientAddr_;
    helper_->dropConnection(reason);
  }

  void dumpConnectionState(uint8_t /* loglevel */) override {}

 protected:
  void connectionReady(
      folly::AsyncTransportWrapper::UniquePtr transport,
      std::string nextProtocol,
      SecureTransportType secureTransportType,
      folly::Optional<SSLErrorEnum>
          details) noexcept override;

  void connectionError(
      folly::AsyncTransportWrapper* transport,
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

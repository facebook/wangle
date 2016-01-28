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

#include <chrono>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSocket.h>
#include <wangle/acceptor/Acceptor.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/acceptor/TransportInfo.h>

namespace wangle {

class AcceptorHandshakeHelper : public folly::DelayedDestruction {
 public:
  using UniquePtr = std::unique_ptr<
    AcceptorHandshakeHelper, folly::DelayedDestruction::Destructor>;

  class Callback {
   public:
    virtual ~Callback() = default;

    virtual void connectionReady(
        folly::AsyncTransportWrapper::UniquePtr transport,
        std::string nextProtocol,
        SecureTransportType secureTransportType) noexcept = 0;

    virtual void connectionError(
        folly::exception_wrapper ex) noexcept = 0;
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

  virtual ~AcceptorHandshakeManager() = default;

  virtual void start(
      folly::AsyncSSLSocket::UniquePtr sock) noexcept {
    acceptor_->getConnectionManager()->addConnection(this, true);
    startHelper(std::move(sock));
  }

  virtual void timeoutExpired() noexcept override {
    VLOG(4) << "SSL handshake timeout expired";
    dropConnection(SSLErrorEnum::TIMEOUT);
  }

  virtual void describe(std::ostream& os) const override {
    os << "pending handshake on " << clientAddr_;
  }

  virtual bool isBusy() const override { return true; }

  virtual void notifyPendingShutdown() override {}

  virtual void closeWhenIdle() override {}

  virtual void dropConnection() override {
    dropConnection(SSLErrorEnum::NO_ERROR);
  }

  void dropConnection(SSLErrorEnum reason) {
    VLOG(10) << "Dropping in progress handshake for " << clientAddr_;
    helper_->dropConnection(reason);
  }

  virtual void dumpConnectionState(uint8_t loglevel) override {}

 protected:
  virtual void connectionReady(
      folly::AsyncTransportWrapper::UniquePtr transport,
      std::string nextProtocol,
      SecureTransportType secureTransportType) noexcept override {
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

  virtual void connectionError(
      folly::exception_wrapper ex) noexcept override {
    acceptor_->getConnectionManager()->removeConnection(this);
    acceptor_->sslConnectionError(std::move(ex));
    destroy();
  }

  virtual void startHelper(folly::AsyncSSLSocket::UniquePtr sock) = 0;

  Acceptor* acceptor_;
  folly::SocketAddress clientAddr_;
  std::chrono::steady_clock::time_point acceptTime_;
  TransportInfo tinfo_;
  AcceptorHandshakeHelper::UniquePtr helper_;
};

}

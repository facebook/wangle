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
#include <wangle/acceptor/SocketPeeker.h>

namespace wangle {

/**
 * A hanshake helper which helpes switching between
 * SSL and other protocols, so that we can run both
 * SSL and other protocols over the same port at the
 * same time.
 * The mechanism used by this is to peek the first numBytes
 * bytes of the socket and send it to the peek helper
 * to decide which protocol it is.
 */
class PeekingAcceptorHandshakeHelper : public AcceptorHandshakeHelper,
                                       public SocketPeeker::Callback {
 public:
  class PeekCallback {
   public:
    explicit PeekCallback(size_t bytesRequired):
      bytesRequired_(bytesRequired) {}

    virtual ~PeekCallback() = default;

    size_t getBytesRequired() const {
      return bytesRequired_;
    }

    virtual AcceptorHandshakeHelper::UniquePtr getHelper(
        const std::vector<uint8_t>& peekedBytes,
        Acceptor* acceptor,
        const folly::SocketAddress& clientAddr,
        std::chrono::steady_clock::time_point acceptTime,
        TransportInfo& tinfo) = 0;

   private:
    const size_t bytesRequired_;
  };

  PeekingAcceptorHandshakeHelper(
      Acceptor* acceptor,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo& tinfo,
      const std::vector<PeekCallback*>& peekCallbacks,
      size_t numBytes)
      : acceptor_(acceptor),
        clientAddr_(clientAddr),
        acceptTime_(acceptTime),
        tinfo_(tinfo),
        peekCallbacks_(peekCallbacks),
        numBytes_(numBytes) {}

  // From AcceptorHandshakeHelper
  virtual void start(
      folly::AsyncSSLSocket::UniquePtr sock,
      AcceptorHandshakeHelper::Callback* callback) noexcept override {
    socket_ = std::move(sock);
    callback_ = callback;
    CHECK_EQ(
        socket_->getSSLState(),
        folly::AsyncSSLSocket::SSLStateEnum::STATE_UNENCRYPTED);
    peeker_.reset(new SocketPeeker(*socket_, this, numBytes_));
    peeker_->start();
  }

  virtual void dropConnection(
      SSLErrorEnum reason = SSLErrorEnum::NO_ERROR) override {
    CHECK_NE(socket_.get() == nullptr, helper_.get() == nullptr);
    if (socket_) {
      socket_->closeNow();
    } else if (helper_) {
      helper_->dropConnection(reason);
    }
  }

  void peekSuccess(std::vector<uint8_t> peekBytes) noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);
    peeker_ = nullptr;

    for (auto& peekCallback : peekCallbacks_) {
      helper_ = peekCallback->getHelper(
          peekBytes, acceptor_, clientAddr_, acceptTime_, tinfo_);
      if (helper_) {
        break;
      }
    }

    if (!helper_) {
      // could not get a helper, report error.
      auto type =
          folly::AsyncSocketException::AsyncSocketExceptionType::CORRUPTED_DATA;
      return peekError(
          folly::AsyncSocketException(type, "Unrecognized protocol"));
    }

    auto callback = callback_;
    callback_ = nullptr;
    helper_->start(std::move(socket_), callback);
    CHECK(!socket_);
  }

  void peekError(const folly::AsyncSocketException& ex) noexcept override {
    peeker_ = nullptr;
    auto callback = callback_;
    callback_ = nullptr;
    callback->connectionError(folly::exception_wrapper(ex));
  }

 private:
  ~PeekingAcceptorHandshakeHelper() = default;

  folly::AsyncSSLSocket::UniquePtr socket_;
  AcceptorHandshakeHelper::UniquePtr helper_;
  SocketPeeker::UniquePtr peeker_;

  Acceptor* acceptor_;
  AcceptorHandshakeHelper::Callback* callback_;
  const folly::SocketAddress& clientAddr_;
  std::chrono::steady_clock::time_point acceptTime_;
  TransportInfo& tinfo_;
  const std::vector<PeekCallback*>& peekCallbacks_;
  size_t numBytes_;
};

using PeekingCallbackPtr = PeekingAcceptorHandshakeHelper::PeekCallback*;

class PeekingAcceptorHandshakeManager : public AcceptorHandshakeManager {
 public:
  PeekingAcceptorHandshakeManager(
        Acceptor* acceptor,
        const folly::SocketAddress& clientAddr,
        std::chrono::steady_clock::time_point acceptTime,
        TransportInfo tinfo,
        const std::vector<PeekingCallbackPtr>& peekCallbacks,
        size_t numBytes):
      AcceptorHandshakeManager(
          acceptor,
          clientAddr,
          acceptTime,
          std::move(tinfo)),
      peekCallbacks_(peekCallbacks),
      numBytes_(numBytes) {}

 protected:
  virtual void startHelper(folly::AsyncSSLSocket::UniquePtr sock) override {
    helper_.reset(new PeekingAcceptorHandshakeHelper(
        acceptor_,
        clientAddr_,
        acceptTime_,
        tinfo_,
        peekCallbacks_,
        numBytes_));
    helper_->start(std::move(sock), this);
  }

  const std::vector<PeekingCallbackPtr>& peekCallbacks_;
  size_t numBytes_;
};

}

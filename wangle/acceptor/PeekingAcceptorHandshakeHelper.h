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
 * The mechanism used by this is to peek the first N
 * bytes of the socket and send it to the peek helper
 * to decide which protocol it is.
 */
template <size_t N>
class PeekingAcceptorHandshakeHelper : public AcceptorHandshakeHelper,
                                       public SocketPeeker<N>::Callback {
 public:
  class PeekCallback {
   public:
    virtual ~PeekCallback() {}

    virtual AcceptorHandshakeHelper::UniquePtr getHelper(
        std::array<uint8_t, N> peekedBytes,
        Acceptor* acceptor,
        const folly::SocketAddress& clientAddr,
        std::chrono::steady_clock::time_point acceptTime,
        TransportInfo& tinfo) = 0;
  };

  PeekingAcceptorHandshakeHelper(
      Acceptor* acceptor,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo& tinfo,
      PeekCallback* peekCallback)
      : acceptor_(acceptor),
        clientAddr_(clientAddr),
        acceptTime_(acceptTime),
        tinfo_(tinfo),
        peekCallback_(peekCallback) {}

  // From AcceptorHandshakeHelper
  virtual void start(
      folly::AsyncSSLSocket::UniquePtr sock,
      AcceptorHandshakeHelper::Callback* callback) noexcept override {
    socket_ = std::move(sock);
    callback_ = callback;
    CHECK_EQ(
        socket_->getSSLState(),
        folly::AsyncSSLSocket::SSLStateEnum::STATE_UNENCRYPTED);
    peeker_.reset(new SocketPeeker<N>(*socket_, this));
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

  void peekSuccess(std::array<uint8_t, N> peekBytes) noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);
    peeker_ = nullptr;

    helper_ = peekCallback_->getHelper(
        std::move(peekBytes), acceptor_, clientAddr_, acceptTime_, tinfo_);

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
  typename SocketPeeker<N>::UniquePtr peeker_;

  Acceptor* acceptor_;
  AcceptorHandshakeHelper::Callback* callback_;
  const folly::SocketAddress& clientAddr_;
  std::chrono::steady_clock::time_point acceptTime_;
  TransportInfo& tinfo_;
  PeekCallback* peekCallback_;
};

template<size_t N>
class PeekingAcceptorHandshakeManager : public AcceptorHandshakeManager {
 public:
  PeekingAcceptorHandshakeManager(
        Acceptor* acceptor,
        const folly::SocketAddress& clientAddr,
        std::chrono::steady_clock::time_point acceptTime,
        TransportInfo tinfo,
        typename PeekingAcceptorHandshakeHelper<N>::PeekCallback* peekCallback):
      AcceptorHandshakeManager(
          acceptor,
          clientAddr,
          acceptTime,
          std::move(tinfo)),
      peekCallback_(peekCallback) {}

 protected:
  virtual void startHelper(folly::AsyncSSLSocket::UniquePtr sock) override {
    helper_.reset(new PeekingAcceptorHandshakeHelper<N>(
        acceptor_,
        clientAddr_,
        acceptTime_,
        tinfo_,
        peekCallback_));
    helper_->start(std::move(sock), this);
  }

  typename PeekingAcceptorHandshakeHelper<N>::PeekCallback* peekCallback_;
};

}

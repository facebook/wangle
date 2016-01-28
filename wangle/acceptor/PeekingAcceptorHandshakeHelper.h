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
 * A hanshake helper which helpes switching between
 * SSL and other protocols, so that we can run both
 * SSL and other protocols over the same port at the
 * same time.
 * The mechanism used by this is to peek the first N
 * bytes of the socket and send it to the peek helper
 * to decide which protocol it is.
 */
template<size_t N>
class PeekingAcceptorHandshakeHelper :
  public AcceptorHandshakeHelper,
  public folly::AsyncTransportWrapper::ReadCallback {
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
        PeekCallback* peekCallback) :
      acceptor_(acceptor),
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
      socket_->setPeek(true);
      socket_->setReadCB(this);
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

    void getReadBuffer(
        void** bufReturn,
        size_t* lenReturn) override {
      *bufReturn = reinterpret_cast<void*>(peekBytes_.data());
      *lenReturn = N;
    }

    void readDataAvailable(size_t len) noexcept override {
      folly::DelayedDestruction::DestructorGuard dg(this);

      // Peek does not advance the socket buffer, so we will
      // always re-read the existing bytes, so we should only
      // consider it a successful peek if we read all N bytes.
      if (len != N) {
        return;
      }
      socket_->setPeek(false);
      socket_->setReadCB(nullptr);

      helper_ = peekCallback_->getHelper(
          std::move(peekBytes_),
          acceptor_,
          clientAddr_,
          acceptTime_,
          tinfo_);

      if (!helper_) {
        // could not get a helper, report error.
        auto type =
          folly::AsyncSocketException::AsyncSocketExceptionType::CORRUPTED_DATA;
        return readErr(
            folly::AsyncSocketException(type, "Unrecognized protocol"));
      }

      auto callback = callback_;
      callback_ = nullptr;
      helper_->start(
          std::move(socket_),
          callback);
      CHECK(!socket_);
    }

    void readEOF() noexcept override {
      folly::DelayedDestruction::DestructorGuard dg(this);

      auto type =
        folly::AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE;
      readErr(folly::AsyncSocketException(type, "Unexpected EOF"));
    }

    void readErr(const folly::AsyncSocketException& ex) noexcept override {
      folly::DelayedDestruction::DestructorGuard dg(this);

      if (callback_) {
        auto callback = callback_;
        callback_ = nullptr;
        callback->connectionError(folly::exception_wrapper(ex));
      }
    }

    bool isBufferMovable() noexcept override {
      // Returning false so that we can supply the exact length of the
      // number of bytes we want to read.
      return false;
    }

    ~PeekingAcceptorHandshakeHelper() {
      if (socket_) {
        socket_->setReadCB(nullptr);
      }
    }

  private:
    folly::AsyncSSLSocket::UniquePtr socket_;
    AcceptorHandshakeHelper::UniquePtr helper_;

    Acceptor* acceptor_;
    AcceptorHandshakeHelper::Callback* callback_;
    const folly::SocketAddress& clientAddr_;
    std::chrono::steady_clock::time_point acceptTime_;
    TransportInfo& tinfo_;

    std::array<uint8_t, N> peekBytes_;
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

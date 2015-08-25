#pragma once

#include <wangle/acceptor/AcceptorHandshakeHelper.h>

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
    class Callback {
      public:
        virtual ~Callback() {}

        virtual folly::Optional<SecureTransportType> getSecureTransportType(
            std::array<uint8_t, N> peekedBytes) = 0;
    };

    PeekingAcceptorHandshakeHelper(
        folly::AsyncSSLSocket::UniquePtr sock,
        Acceptor* acceptor,
        const folly::SocketAddress& clientAddr,
        std::chrono::steady_clock::time_point acceptTime,
        TransportInfo tinfo,
        Callback* peekCallback) :
      AcceptorHandshakeHelper(
          std::move(sock),
          acceptor,
          clientAddr,
          acceptTime,
          std::move(tinfo)),
      peekCallback_(peekCallback) {}

    // From AcceptorHandshakeHelper
    void start() noexcept override {
      CHECK_EQ(
          socket_->getSSLState(),
          folly::AsyncSSLSocket::SSLStateEnum::STATE_UNENCRYPTED);
      socket_->setPeek(true);
      socket_->setReadCB(this);
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

      auto secureTransportType =
        peekCallback_->getSecureTransportType(std::move(peekBytes_));
      if (!secureTransportType) {
        // could not get a transport, report error.
        auto type =
         folly::AsyncSocketException::AsyncSocketExceptionType::CORRUPTED_DATA;
        return handshakeErr(
            socket_.get(),
            folly::AsyncSocketException(type, "Unrecognized protocol"));
      }

      if (*secureTransportType == SecureTransportType::TLS) {
        return socket_->sslAccept(this);
      }

      acceptor_->sslConnectionReady(
          std::move(socket_),
          clientAddr_,
          "",
          *secureTransportType,
          tinfo_);
      destroy();
    }

    void readEOF() noexcept override {
      auto type =
        folly::AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE;
      handshakeErr(
          socket_.get(),
          folly::AsyncSocketException(type, "Unexpected EOF"));
    }

    void readErr(const folly::AsyncSocketException& ex) noexcept override {
      handshakeErr(socket_.get(), ex);
    }

    bool isBufferMovable() noexcept override {
      // Returning false so that we can supply the exact length of the
      // number of bytes we want to read.
      return false;
    }

  private:
    std::array<uint8_t, N> peekBytes_;
    Callback* peekCallback_;
};

}

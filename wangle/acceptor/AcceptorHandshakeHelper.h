#pragma once

#include <chrono>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSocket.h>
#include <wangle/acceptor/Acceptor.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/acceptor/TransportInfo.h>

namespace wangle {

class AcceptorHandshakeHelper : public folly::AsyncSSLSocket::HandshakeCB,
                                public ManagedConnection {
  public:
    AcceptorHandshakeHelper(
        folly::AsyncSSLSocket::UniquePtr sock,
        Acceptor* acceptor,
        const folly::SocketAddress& clientAddr,
        std::chrono::steady_clock::time_point acceptTime,
        TransportInfo tinfo) :
      socket_(std::move(sock)), acceptor_(acceptor),
      clientAddr_(clientAddr), acceptTime_(acceptTime),
      tinfo_(std::move(tinfo)) {
      acceptor_->downstreamConnectionManager_->addConnection(this, true);
      if (acceptor_->parseClientHello_) {
        socket_->enableClientHelloParsing();
      }
    }

    virtual void start() noexcept;

    void timeoutExpired() noexcept override {
      VLOG(4) << "SSL handshake timeout expired";
      sslError_ = SSLErrorEnum::TIMEOUT;
      dropConnection();
    }

    void describe(std::ostream& os) const override {
      os << "pending handshake on " << clientAddr_;
    }

    bool isBusy() const override { return true; }

    void notifyPendingShutdown() override {}

    void closeWhenIdle() override {}

    void dropConnection() override {
      VLOG(10) << "Dropping in progress handshake for " << clientAddr_;
      socket_->closeNow();
    }

    void dumpConnectionState(uint8_t loglevel) override {}

  protected:
    // AsyncSSLSocket::HandshakeCallback API
    void handshakeSuc(folly::AsyncSSLSocket* sock) noexcept override;
    void handshakeErr(folly::AsyncSSLSocket* sock,
                      const folly::AsyncSocketException& ex) noexcept override;

    folly::AsyncSSLSocket::UniquePtr socket_;
    Acceptor* acceptor_;
    folly::SocketAddress clientAddr_;
    std::chrono::steady_clock::time_point acceptTime_;
    TransportInfo tinfo_;
    SSLErrorEnum sslError_{SSLErrorEnum::NO_ERROR};
};

}

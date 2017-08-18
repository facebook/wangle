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
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSocket.h>
#include <wangle/acceptor/AcceptorHandshakeManager.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/acceptor/PeekingAcceptorHandshakeHelper.h>
#include <wangle/acceptor/TransportInfo.h>

namespace wangle {

class SSLAcceptorHandshakeHelper : public AcceptorHandshakeHelper,
                                   public folly::AsyncSSLSocket::HandshakeCB {
 public:
  SSLAcceptorHandshakeHelper(
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo& tinfo) :
    clientAddr_(clientAddr),
    acceptTime_(acceptTime),
    tinfo_(tinfo) {}

  void start(
      folly::AsyncSSLSocket::UniquePtr sock,
      AcceptorHandshakeHelper::Callback* callback) noexcept override;

  void dropConnection(SSLErrorEnum reason = SSLErrorEnum::NO_ERROR) override {
    sslError_ = reason;
    socket_->closeNow();
  }

  static void fillSSLTransportInfoFields(
      folly::AsyncSSLSocket* sock, TransportInfo& tinfo);

 protected:
  // AsyncSSLSocket::HandshakeCallback API
  void handshakeSuc(folly::AsyncSSLSocket* sock) noexcept override;
  void handshakeErr(folly::AsyncSSLSocket* sock,
                    const folly::AsyncSocketException& ex) noexcept override;

  folly::AsyncSSLSocket::UniquePtr socket_;
  AcceptorHandshakeHelper::Callback* callback_;
  const folly::SocketAddress& clientAddr_;
  std::chrono::steady_clock::time_point acceptTime_;
  TransportInfo& tinfo_;
  SSLErrorEnum sslError_{SSLErrorEnum::NO_ERROR};
};

class DefaultToSSLPeekingCallback :
  public PeekingAcceptorHandshakeHelper::PeekCallback {
 public:
  DefaultToSSLPeekingCallback():
    PeekingAcceptorHandshakeHelper::PeekCallback(0) {}

  AcceptorHandshakeHelper::UniquePtr getHelper(
      const std::vector<uint8_t>& /* bytes */,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo& tinfo) override {
    return AcceptorHandshakeHelper::UniquePtr(new SSLAcceptorHandshakeHelper(
        clientAddr, acceptTime, tinfo));
  }
};

}

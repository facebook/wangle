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

#include <fizz/extensions/tokenbinding/TokenBindingContext.h>
#include <fizz/extensions/tokenbinding/TokenBindingServerExtension.h>
#include <fizz/server/AsyncFizzServer.h>
#include <wangle/acceptor/AcceptorHandshakeManager.h>
#include <wangle/acceptor/PeekingAcceptorHandshakeHelper.h>

namespace wangle {

class FizzHandshakeException : public wangle::SSLException {
 public:
  FizzHandshakeException(
      SSLErrorEnum error,
      const std::chrono::milliseconds& latency,
      uint64_t bytesRead,
      folly::exception_wrapper ex)
      : wangle::SSLException(error, latency, bytesRead),
        originalException_(std::move(ex)) {}

  const folly::exception_wrapper& getOriginalException() const {
    return originalException_;
  }

  using wangle::SSLException::SSLException;

 private:
  folly::exception_wrapper originalException_;
};

class FizzAcceptorHandshakeHelper
    : public wangle::AcceptorHandshakeHelper,
      public fizz::server::AsyncFizzServer::HandshakeCallback,
      public folly::AsyncSSLSocket::HandshakeCB {
 public:
  class LoggingCallback {
   public:
    virtual ~LoggingCallback() = default;
    virtual void logFizzHandshakeSuccess(
        const fizz::server::AsyncFizzServer&,
        const wangle::TransportInfo* tinfo) noexcept = 0;
    virtual void logFizzHandshakeFallback(
        const fizz::server::AsyncFizzServer&,
        const wangle::TransportInfo* tinfo) noexcept = 0;
    virtual void logFizzHandshakeError(
        const fizz::server::AsyncFizzServer&,
        const folly::exception_wrapper&) noexcept = 0;
  };

  FizzAcceptorHandshakeHelper(
      std::shared_ptr<const fizz::server::FizzServerContext> context,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      wangle::TransportInfo& tinfo,
      LoggingCallback* loggingCallback,
      const std::shared_ptr<fizz::extensions::TokenBindingContext>&
          tokenBindingContext)
      : context_(context),
        tokenBindingContext_(tokenBindingContext),
        clientAddr_(clientAddr),
        acceptTime_(acceptTime),
        tinfo_(tinfo),
        loggingCallback_(loggingCallback) {}

  void start(
      folly::AsyncSSLSocket::UniquePtr sock,
      wangle::AcceptorHandshakeHelper::Callback* callback) noexcept override;

  void dropConnection(
      wangle::SSLErrorEnum reason = wangle::SSLErrorEnum::NO_ERROR) override {
    sslError_ = reason;
    if (transport_) {
      transport_->closeNow();
      return;
    }
    if (sslSocket_) {
      sslSocket_->closeNow();
      return;
    }
  }

 protected:
  virtual fizz::server::AsyncFizzServer::UniquePtr createFizzServer(
      folly::AsyncSSLSocket::UniquePtr sslSock,
      const std::shared_ptr<const fizz::server::FizzServerContext>& fizzContext,
      const std::shared_ptr<fizz::ServerExtensions>& extensions);

  virtual folly::AsyncSSLSocket::UniquePtr createSSLSocket(
      const std::shared_ptr<folly::SSLContext>& sslContext,
      folly::AsyncTransport::UniquePtr transport);

  // AsyncFizzServer::HandshakeCallback API
  void fizzHandshakeSuccess(
      fizz::server::AsyncFizzServer* transport) noexcept override;
  void fizzHandshakeError(
      fizz::server::AsyncFizzServer* transport,
      folly::exception_wrapper ex) noexcept override;
  void fizzHandshakeAttemptFallback(
     std::unique_ptr<folly::IOBuf> clientHello) override;

  // AsyncSSLSocket::HandshakeCallback API
  void handshakeSuc(folly::AsyncSSLSocket* sock) noexcept override;
  void handshakeErr(
      folly::AsyncSSLSocket* sock,
      const folly::AsyncSocketException& ex) noexcept override;

  std::shared_ptr<const fizz::server::FizzServerContext> context_;
  std::shared_ptr<folly::SSLContext> sslContext_;
  std::shared_ptr<fizz::extensions::TokenBindingContext> tokenBindingContext_;
  std::shared_ptr<fizz::extensions::TokenBindingServerExtension>
      tokenBindingExtension_;
  fizz::server::AsyncFizzServer::UniquePtr transport_;
  folly::AsyncSSLSocket::UniquePtr sslSocket_;
  wangle::AcceptorHandshakeHelper::Callback* callback_;
  const folly::SocketAddress& clientAddr_;
  std::chrono::steady_clock::time_point acceptTime_;
  wangle::TransportInfo& tinfo_;
  wangle::SSLErrorEnum sslError_{wangle::SSLErrorEnum::NO_ERROR};
  LoggingCallback* loggingCallback_;
};

class DefaultToFizzPeekingCallback
    : public wangle::PeekingAcceptorHandshakeHelper::PeekCallback {
 public:
  DefaultToFizzPeekingCallback()
      : wangle::PeekingAcceptorHandshakeHelper::PeekCallback(0) {}

  std::shared_ptr<const fizz::server::FizzServerContext> getContext() const {
    return context_;
  }

  void setContext(
      std::shared_ptr<const fizz::server::FizzServerContext> context) {
    context_ = std::move(context);
  }

  std::shared_ptr<fizz::extensions::TokenBindingContext>
  getTokenBindingContext() const {
    return tokenBindingContext_;
  }

  void setTokenBindingContext(
      std::shared_ptr<fizz::extensions::TokenBindingContext> context) {
    tokenBindingContext_ = std::move(context);
  }

  void setLoggingCallback(
      FizzAcceptorHandshakeHelper::LoggingCallback* loggingCallback) {
    loggingCallback_ = loggingCallback;
  }

  wangle::AcceptorHandshakeHelper::UniquePtr getHelper(
      const std::vector<uint8_t>& /* bytes */,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      wangle::TransportInfo& tinfo) override {
    return wangle::AcceptorHandshakeHelper::UniquePtr(
        new FizzAcceptorHandshakeHelper(
            context_,
            clientAddr,
            acceptTime,
            tinfo,
            loggingCallback_,
            tokenBindingContext_));
  }

 protected:
  std::shared_ptr<const fizz::server::FizzServerContext> context_;
  std::shared_ptr<fizz::extensions::TokenBindingContext> tokenBindingContext_;
  FizzAcceptorHandshakeHelper::LoggingCallback* loggingCallback_{nullptr};
};
} // namespace wangle

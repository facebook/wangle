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

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBaseManager.h>
#include <wangle/bootstrap/BaseClientBootstrap.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>

namespace wangle {

/*
 * A thin wrapper around Pipeline and AsyncSocket to match
 * ServerBootstrap.  On connect() a new pipeline is created.
 */
template <typename Pipeline>
class ClientBootstrap : public BaseClientBootstrap<Pipeline> {
  class ConnectCallback : public folly::AsyncSocket::ConnectCallback {
   public:
    ConnectCallback(folly::Promise<Pipeline*> promise, ClientBootstrap* bootstrap)
        : promise_(std::move(promise))
        , bootstrap_(bootstrap) {}

    void connectSuccess() noexcept override {
      if (bootstrap_->getPipeline()) {
        bootstrap_->getPipeline()->transportActive();
      }
      promise_.setValue(bootstrap_->getPipeline());
      delete this;
    }

    void connectErr(const folly::AsyncSocketException& ex) noexcept override {
      promise_.setException(
        folly::make_exception_wrapper<folly::AsyncSocketException>(ex));
      delete this;
    }
   private:
    folly::Promise<Pipeline*> promise_;
    ClientBootstrap* bootstrap_;
  };

 public:
  ClientBootstrap() {
  }

  ClientBootstrap* group(
      std::shared_ptr<wangle::IOThreadPoolExecutor> group) {
    group_ = group;
    return this;
  }

  ClientBootstrap* bind(int port) {
    port_ = port;
    return this;
  }

  folly::Future<Pipeline*> connect(
      const folly::SocketAddress& address,
      std::chrono::milliseconds timeout =
          std::chrono::milliseconds(0)) override {
    auto base = folly::EventBaseManager::get()->getEventBase();
    if (group_) {
      base = group_->getEventBase();
    }
    folly::Future<Pipeline*> retval((Pipeline*)nullptr);
    base->runImmediatelyOrRunInEventBaseThreadAndWait([&](){
      std::shared_ptr<folly::AsyncSocket> socket;
      if (this->sslContext_) {
        auto sslSocket =
            folly::AsyncSSLSocket::newSocket(this->sslContext_, base);
        if (this->sslSession_) {
          sslSocket->setSSLSession(this->sslSession_, true);
        }
        socket = sslSocket;
      } else {
        socket = folly::AsyncSocket::newSocket(base);
      }
      folly::Promise<Pipeline*> promise;
      retval = promise.getFuture();
      socket->connect(
          new ConnectCallback(std::move(promise), this),
          address,
          timeout.count());
      BaseClientBootstrap<Pipeline>::makePipeline(socket);
    });
    return retval;
  }

  virtual ~ClientBootstrap() = default;

 protected:
  int port_;
  std::shared_ptr<wangle::IOThreadPoolExecutor> group_;
};

class ClientBootstrapFactory
    : public BaseClientBootstrapFactory<BaseClientBootstrap<>> {
 public:
  ClientBootstrapFactory() {}

  BaseClientBootstrap<>::Ptr newClient() override {
    return folly::make_unique<ClientBootstrap<DefaultPipeline>>();
  }
};

} // namespace wangle

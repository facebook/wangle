/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/DestructorCheck.h>
#include <folly/io/async/EventBaseManager.h>
#include <wangle/bootstrap/BaseClientBootstrap.h>
#include <wangle/channel/Pipeline.h>
#include <folly/executors/IOThreadPoolExecutor.h>

namespace wangle {

/*
 * A thin wrapper around Pipeline and AsyncSocket to match
 * ServerBootstrap.  On connect() a new pipeline is created.
 */
template <typename Pipeline>
class ClientBootstrap : public BaseClientBootstrap<Pipeline>,
                        public folly::DestructorCheck {
  class ConnectCallback : public folly::AsyncSocket::ConnectCallback {
   public:
    ConnectCallback(
        folly::Promise<Pipeline*> promise,
        ClientBootstrap* bootstrap,
        std::shared_ptr<folly::AsyncSocket> socket)
        : promise_(std::move(promise)),
          bootstrap_(bootstrap),
          socket_(socket),
          safety_(*bootstrap) {}

    void connectSuccess() noexcept override {
      if (!safety_.destroyed()) {
        bootstrap_->makePipeline(std::move(socket_));
        if (bootstrap_->getPipeline()) {
          bootstrap_->getPipeline()->transportActive();
        }
        promise_.setValue(bootstrap_->getPipeline());
      }
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
    std::shared_ptr<folly::AsyncSocket> socket_;
    folly::DestructorCheck::Safety safety_;
  };

 public:
  ClientBootstrap() {
  }

  ClientBootstrap* group(
      std::shared_ptr<folly::IOThreadPoolExecutor> group) {
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
    auto base = (group_)
      ? group_->getEventBase()
      : folly::EventBaseManager::get()->getEventBase();
    folly::Future<Pipeline*> retval((Pipeline*)nullptr);
    base->runImmediatelyOrRunInEventBaseThreadAndWait([&](){
      std::shared_ptr<folly::AsyncSocket> socket;
      if (this->sslContext_) {
        auto sslSocket = folly::AsyncSSLSocket::newSocket(
            this->sslContext_, base, this->deferSecurityNegotiation_);
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
          new ConnectCallback(std::move(promise), this, socket),
          address,
          timeout.count());
    });
    return retval;
  }

  ~ClientBootstrap() override = default;

 protected:
  int port_;
  std::shared_ptr<folly::IOThreadPoolExecutor> group_;
};

class ClientBootstrapFactory
    : public BaseClientBootstrapFactory<BaseClientBootstrap<>> {
 public:
  ClientBootstrapFactory() {}

  BaseClientBootstrap<>::Ptr newClient() override {
    return std::make_unique<ClientBootstrap<DefaultPipeline>>();
  }
};

} // namespace wangle

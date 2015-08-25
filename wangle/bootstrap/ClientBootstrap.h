/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <wangle/channel/Pipeline.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBaseManager.h>

namespace wangle {

/*
 * A thin wrapper around Pipeline and AsyncSocket to match
 * ServerBootstrap.  On connect() a new pipeline is created.
 */
template <typename Pipeline>
class ClientBootstrap {

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
      folly::SocketAddress address,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
    DCHECK(pipelineFactory_);
    auto base = folly::EventBaseManager::get()->getEventBase();
    if (group_) {
      base = group_->getEventBase();
    }
    folly::Future<Pipeline*> retval((Pipeline*)nullptr);
    base->runImmediatelyOrRunInEventBaseThreadAndWait([&](){
      auto socket = folly::AsyncSocket::newSocket(base);
      folly::Promise<Pipeline*> promise;
      retval = promise.getFuture();
      socket->connect(
          new ConnectCallback(std::move(promise), this),
          address,
          timeout.count());
      pipeline_ = pipelineFactory_->newPipeline(socket);
    });
    return retval;
  }

  ClientBootstrap* pipelineFactory(
      std::shared_ptr<PipelineFactory<Pipeline>> factory) {
    pipelineFactory_ = factory;
    return this;
  }

  Pipeline* getPipeline() {
    return pipeline_.get();
  }

  virtual ~ClientBootstrap() = default;

 protected:
  std::unique_ptr<Pipeline,
                  folly::DelayedDestruction::Destructor> pipeline_;

  int port_;

  std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory_;
  std::shared_ptr<wangle::IOThreadPoolExecutor> group_;
};

} // namespace wangle

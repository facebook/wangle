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

#include <folly/SocketAddress.h>
#include <folly/futures/Future.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <wangle/channel/Pipeline.h>
#include <memory>

namespace wangle {

/*
 * A wrapper template around Pipeline and AsyncSocket or SPDY/HTTP/2 session to
 * match ServerBootstrap so BroadcastPool can work with either option
 */
template <typename P = DefaultPipeline>
class BaseClientBootstrap {
 public:
  using Ptr = std::unique_ptr<BaseClientBootstrap>;
  BaseClientBootstrap() {}

  virtual ~BaseClientBootstrap() = default;

  BaseClientBootstrap<P>* pipelineFactory(
      std::shared_ptr<PipelineFactory<P>> factory) noexcept {
    pipelineFactory_ = factory;
    return this;
  }

  P* getPipeline() {
    return pipeline_.get();
  }

  virtual folly::Future<P*> connect(
      const folly::SocketAddress& address,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) = 0;

  BaseClientBootstrap* sslContext(folly::SSLContextPtr sslContext) {
    sslContext_ = sslContext;
    return this;
  }

  BaseClientBootstrap* sslSession(SSL_SESSION* sslSession) {
    sslSession_ = sslSession;
    return this;
  }

  void setPipeline(const typename P::Ptr& pipeline) {
    pipeline_ = pipeline;
  }

  virtual void makePipeline(std::shared_ptr<folly::AsyncSocket> socket) {
    pipeline_ = pipelineFactory_->newPipeline(socket);
  }

 protected:
  std::shared_ptr<PipelineFactory<P>> pipelineFactory_;
  typename P::Ptr pipeline_;
  folly::SSLContextPtr sslContext_;
  SSL_SESSION* sslSession_{nullptr};
};

template <typename ClientBootstrap = BaseClientBootstrap<>>
class BaseClientBootstrapFactory {
 public:
  virtual typename ClientBootstrap::Ptr newClient() = 0;
  virtual ~BaseClientBootstrapFactory() = default;
};

} // namespace wangle

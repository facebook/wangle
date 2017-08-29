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

  BaseClientBootstrap* deferSecurityNegotiation(bool deferSecurityNegotiation) {
    deferSecurityNegotiation_ = deferSecurityNegotiation;
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
  bool deferSecurityNegotiation_{false};
};

template <typename ClientBootstrap = BaseClientBootstrap<>>
class BaseClientBootstrapFactory {
 public:
  virtual typename ClientBootstrap::Ptr newClient() = 0;
  virtual ~BaseClientBootstrapFactory() = default;
};

} // namespace wangle

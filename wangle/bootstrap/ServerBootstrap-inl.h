/*
 * Copyright 2015 Facebook, Inc.
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

#include <wangle/acceptor/Acceptor.h>
#include <wangle/bootstrap/ServerSocketFactory.h>
#include <folly/io/async/EventBaseManager.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/Handler.h>

namespace folly {

template <typename Pipeline>
class ServerAcceptor
    : public Acceptor
    , public folly::wangle::InboundHandler<void*> {
 public:
  class ServerConnection : public wangle::ManagedConnection,
                           public wangle::PipelineManager {
   public:
    explicit ServerConnection(typename Pipeline::UniquePtr pipeline)
        : pipeline_(std::move(pipeline)) {
      pipeline_->setPipelineManager(this);
    }

    ~ServerConnection() {}

    void timeoutExpired() noexcept override {
    }

    void describe(std::ostream& os) const override {}
    bool isBusy() const override {
      return false;
    }
    void notifyPendingShutdown() override {}
    void closeWhenIdle() override {}
    void dropConnection() override {
      delete this;
    }
    void dumpConnectionState(uint8_t loglevel) override {}

    void deletePipeline(wangle::PipelineBase* p) override {
      CHECK(p == pipeline_.get());
      delete this;
    }

   private:
    typename Pipeline::UniquePtr pipeline_;
  };

  explicit ServerAcceptor(
        std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory,
        std::shared_ptr<folly::wangle::Pipeline<void*>> acceptorPipeline,
        EventBase* base)
      : Acceptor(ServerSocketConfig())
      , base_(base)
      , childPipelineFactory_(pipelineFactory)
      , acceptorPipeline_(acceptorPipeline) {
    Acceptor::init(nullptr, base_);
    CHECK(acceptorPipeline_);

    acceptorPipeline_->addBack(this);
    acceptorPipeline_->finalize();
  }

  void read(Context* ctx, void* conn) {
    AsyncSocket::UniquePtr transport((AsyncSocket*)conn);
    typename Pipeline::UniquePtr pipeline(
        childPipelineFactory_->newPipeline(std::shared_ptr<AsyncSocket>(
            transport.release(), folly::DelayedDestruction::Destructor())));
    pipeline->transportActive();
    auto connection = new ServerConnection(std::move(pipeline));
    Acceptor::addConnection(connection);
  }

  /* See Acceptor::onNewConnection for details */
  void onNewConnection(
    AsyncSocket::UniquePtr transport, const SocketAddress* address,
    const std::string& nextProtocolName, const TransportInfo& tinfo) {
    acceptorPipeline_->read(transport.release());
  }

  // UDP thunk
  void onDataAvailable(std::shared_ptr<AsyncUDPSocket> socket,
                       const folly::SocketAddress& addr,
                       std::unique_ptr<folly::IOBuf> buf,
                       bool truncated) noexcept {
    acceptorPipeline_->read(buf.release());
  }

 private:
  EventBase* base_;

  std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory_;
  std::shared_ptr<folly::wangle::Pipeline<void*>> acceptorPipeline_;
};

template <typename Pipeline>
class ServerAcceptorFactory : public AcceptorFactory {
 public:
  explicit ServerAcceptorFactory(
    std::shared_ptr<PipelineFactory<Pipeline>> factory,
    std::shared_ptr<PipelineFactory<folly::wangle::Pipeline<void*>>> pipeline)
    : factory_(factory)
    , pipeline_(pipeline) {}

  std::shared_ptr<Acceptor> newAcceptor(EventBase* base) {
    std::shared_ptr<folly::wangle::Pipeline<void*>> pipeline(
        pipeline_->newPipeline(nullptr));
    return std::make_shared<ServerAcceptor<Pipeline>>(factory_, pipeline, base);
  }
 private:
  std::shared_ptr<PipelineFactory<Pipeline>> factory_;
  std::shared_ptr<PipelineFactory<
    folly::wangle::Pipeline<void*>>> pipeline_;
};

class ServerWorkerPool : public folly::wangle::ThreadPoolExecutor::Observer {
 public:
  explicit ServerWorkerPool(
    std::shared_ptr<AcceptorFactory> acceptorFactory,
    folly::wangle::IOThreadPoolExecutor* exec,
    std::shared_ptr<std::vector<std::shared_ptr<folly::AsyncSocketBase>>> sockets,
    std::shared_ptr<ServerSocketFactory> socketFactory)
      : acceptorFactory_(acceptorFactory)
      , exec_(exec)
      , sockets_(sockets)
      , socketFactory_(socketFactory) {
    CHECK(exec);
  }

  template <typename F>
  void forEachWorker(F&& f) const;

  void threadStarted(
    folly::wangle::ThreadPoolExecutor::ThreadHandle*);
  void threadStopped(
    folly::wangle::ThreadPoolExecutor::ThreadHandle*);
  void threadPreviouslyStarted(
      folly::wangle::ThreadPoolExecutor::ThreadHandle* thread) {
    threadStarted(thread);
  }
  void threadNotYetStopped(
      folly::wangle::ThreadPoolExecutor::ThreadHandle* thread) {
    threadStopped(thread);
  }

 private:
  std::map<folly::wangle::ThreadPoolExecutor::ThreadHandle*,
           std::shared_ptr<Acceptor>> workers_;
  std::shared_ptr<AcceptorFactory> acceptorFactory_;
  folly::wangle::IOThreadPoolExecutor* exec_{nullptr};
  std::shared_ptr<std::vector<std::shared_ptr<folly::AsyncSocketBase>>> sockets_;
  std::shared_ptr<ServerSocketFactory> socketFactory_;
};

template <typename F>
void ServerWorkerPool::forEachWorker(F&& f) const {
  for (const auto& kv : workers_) {
    f(kv.second.get());
  }
}

class DefaultAcceptPipelineFactory
    : public PipelineFactory<wangle::Pipeline<void*>> {
  typedef wangle::Pipeline<void*> AcceptPipeline;

 public:
  AcceptPipeline::UniquePtr newPipeline(std::shared_ptr<AsyncSocket>) {
    return AcceptPipeline::UniquePtr(new AcceptPipeline);
  }
};

} // namespace

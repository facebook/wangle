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

#include <wangle/acceptor/Acceptor.h>
#include <wangle/bootstrap/ServerSocketFactory.h>
#include <folly/io/async/EventBaseManager.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/Handler.h>

namespace wangle {

template <typename Pipeline>
class ServerAcceptor
    : public Acceptor
    , public wangle::InboundHandler<void*> {
 public:
  typedef std::unique_ptr<Pipeline,
                          folly::DelayedDestruction::Destructor> PipelinePtr;

  class ServerConnection : public wangle::ManagedConnection,
                           public wangle::PipelineManager {
   public:
    explicit ServerConnection(PipelinePtr pipeline)
        : pipeline_(std::move(pipeline)) {
      pipeline_->setPipelineManager(this);
    }

    ~ServerConnection() = default;

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
    PipelinePtr pipeline_;
  };

  explicit ServerAcceptor(
        std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory,
        std::shared_ptr<wangle::Pipeline<void*>> acceptorPipeline,
        folly::EventBase* base)
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
    folly::AsyncSocket::UniquePtr transport((folly::AsyncSocket*)conn);
      std::unique_ptr<Pipeline,
                       folly::DelayedDestruction::Destructor>
      pipeline(childPipelineFactory_->newPipeline(
        std::shared_ptr<folly::AsyncSocket>(
          transport.release(),
          folly::DelayedDestruction::Destructor())));
    pipeline->transportActive();
    auto connection = new ServerConnection(std::move(pipeline));
    Acceptor::addConnection(connection);
  }

  /* See Acceptor::onNewConnection for details */
  void onNewConnection(
    folly::AsyncSocket::UniquePtr transport,
    const folly::SocketAddress* address,
    const std::string& nextProtocolName,
    SecureTransportType secureTransportType,
    const TransportInfo& tinfo) {
    acceptorPipeline_->read(transport.release());
  }

  // UDP thunk
  void onDataAvailable(std::shared_ptr<folly::AsyncUDPSocket> socket,
                       const folly::SocketAddress& addr,
                       std::unique_ptr<folly::IOBuf> buf,
                       bool truncated) noexcept {
    acceptorPipeline_->read(buf.release());
  }

 private:
  folly::EventBase* base_;

  std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory_;
  std::shared_ptr<wangle::Pipeline<void*>> acceptorPipeline_;
};

template <typename Pipeline>
class ServerAcceptorFactory : public AcceptorFactory {
 public:
  explicit ServerAcceptorFactory(
    std::shared_ptr<PipelineFactory<Pipeline>> factory,
    std::shared_ptr<PipelineFactory<wangle::Pipeline<void*>>> pipeline)
    : factory_(factory)
    , pipeline_(pipeline) {}

  std::shared_ptr<Acceptor> newAcceptor(folly::EventBase* base) {
    std::shared_ptr<wangle::Pipeline<void*>> pipeline(
        pipeline_->newPipeline(nullptr));
    return std::make_shared<ServerAcceptor<Pipeline>>(factory_, pipeline, base);
  }
 private:
  std::shared_ptr<PipelineFactory<Pipeline>> factory_;
  std::shared_ptr<PipelineFactory<
    wangle::Pipeline<void*>>> pipeline_;
};

class ServerWorkerPool : public wangle::ThreadPoolExecutor::Observer {
 public:
  explicit ServerWorkerPool(
    std::shared_ptr<AcceptorFactory> acceptorFactory,
    wangle::IOThreadPoolExecutor* exec,
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
    wangle::ThreadPoolExecutor::ThreadHandle*);
  void threadStopped(
    wangle::ThreadPoolExecutor::ThreadHandle*);
  void threadPreviouslyStarted(
      wangle::ThreadPoolExecutor::ThreadHandle* thread) {
    threadStarted(thread);
  }
  void threadNotYetStopped(
      wangle::ThreadPoolExecutor::ThreadHandle* thread) {
    threadStopped(thread);
  }

 private:
  std::map<wangle::ThreadPoolExecutor::ThreadHandle*,
           std::shared_ptr<Acceptor>> workers_;
  std::shared_ptr<AcceptorFactory> acceptorFactory_;
  wangle::IOThreadPoolExecutor* exec_{nullptr};
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

 public:
  wangle::AcceptPipeline::UniquePtr newPipeline(std::shared_ptr<folly::AsyncSocket>) {
    return wangle::AcceptPipeline::UniquePtr(new wangle::AcceptPipeline);
  }
};

} // namespace wangle

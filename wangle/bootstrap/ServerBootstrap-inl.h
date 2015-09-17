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
#include <folly/SharedMutex.h>

namespace wangle {

template <typename Pipeline>
class ServerAcceptor
    : public Acceptor
    , public wangle::InboundHandler<AcceptPipelineType> {
 public:
  class ServerConnection : public wangle::ManagedConnection,
                           public wangle::PipelineManager {
   public:
    explicit ServerConnection(typename Pipeline::Ptr pipeline)
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
    typename Pipeline::Ptr pipeline_;
  };

  explicit ServerAcceptor(
      std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory,
      std::shared_ptr<wangle::Pipeline<AcceptPipelineType>> acceptorPipeline,
      folly::EventBase* base,
      const ServerSocketConfig& accConfig)
      : Acceptor(accConfig),
        base_(base),
        childPipelineFactory_(pipelineFactory),
        acceptorPipeline_(acceptorPipeline) {
    Acceptor::init(nullptr, base_);
    CHECK(acceptorPipeline_);

    if (childPipelineFactory_) {
      acceptorPipeline_->addBack(this);
    }
    acceptorPipeline_->finalize();
  }

  void read(Context* ctx, AcceptPipelineType conn) {
    // Did you mean to use pipeline() instead of childPipeline() ?
    auto connInfo = boost::get<ConnInfo&>(conn);

    folly::AsyncSocket::UniquePtr transport(connInfo.sock);

    // setup local and remote addresses
    auto tInfoPtr = folly::make_unique<TransportInfo>(connInfo.tinfo);
    tInfoPtr->localAddr = std::make_shared<folly::SocketAddress>();
    transport->getLocalAddress(tInfoPtr->localAddr.get());
    tInfoPtr->remoteAddr =
      std::make_shared<folly::SocketAddress>(*connInfo.clientAddr);
    tInfoPtr->sslNextProtocol =
      std::make_shared<std::string>(connInfo.nextProtoName);

    auto pipeline = childPipelineFactory_->newPipeline(
      std::shared_ptr<folly::AsyncSocket>(
        transport.release(), folly::DelayedDestruction::Destructor()));
    pipeline->setTransportInfo(std::move(tInfoPtr));
    pipeline->transportActive();
    auto connection = new ServerConnection(std::move(pipeline));
    Acceptor::addConnection(connection);
  }

  /* See Acceptor::onNewConnection for details */
  void onNewConnection(
    folly::AsyncSocket::UniquePtr transport,
    const folly::SocketAddress* clientAddr,
    const std::string& nextProtocolName,
    SecureTransportType secureTransportType,
    const TransportInfo& tinfo) {
    ConnInfo connInfo = {transport.release(), clientAddr, nextProtocolName,
                         secureTransportType, tinfo};
    acceptorPipeline_->read(connInfo);
  }

  // UDP thunk
  void onDataAvailable(std::shared_ptr<folly::AsyncUDPSocket> socket,
                       const folly::SocketAddress& addr,
                       std::unique_ptr<folly::IOBuf> buf,
                       bool truncated) noexcept {
    acceptorPipeline_->read(
        AcceptPipelineType(make_tuple(buf.release(), socket, addr)));
  }

 private:
  folly::EventBase* base_;

  std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory_;
  std::shared_ptr<wangle::Pipeline<AcceptPipelineType>> acceptorPipeline_;
};

template <typename Pipeline>
class ServerAcceptorFactory : public AcceptorFactory {
 public:
  explicit ServerAcceptorFactory(
      std::shared_ptr<PipelineFactory<Pipeline>> factory,
      std::shared_ptr<PipelineFactory<wangle::Pipeline<AcceptPipelineType>>>
          pipeline,
      const ServerSocketConfig& accConfig)
      : factory_(factory), pipeline_(pipeline), accConfig_(accConfig) {}

  std::shared_ptr<Acceptor> newAcceptor(folly::EventBase* base) {
    std::shared_ptr<wangle::Pipeline<AcceptPipelineType>> pipeline(
        pipeline_->newPipeline(nullptr));
    return std::make_shared<ServerAcceptor<Pipeline>>(
        factory_, pipeline, base, accConfig_);
  }
 private:
  std::shared_ptr<PipelineFactory<Pipeline>> factory_;
  std::shared_ptr<PipelineFactory<
    wangle::Pipeline<AcceptPipelineType>>> pipeline_;
  ServerSocketConfig accConfig_;
};

class ServerWorkerPool : public wangle::ThreadPoolExecutor::Observer {
 public:
  explicit ServerWorkerPool(
    std::shared_ptr<AcceptorFactory> acceptorFactory,
    wangle::IOThreadPoolExecutor* exec,
    std::shared_ptr<std::vector<std::shared_ptr<folly::AsyncSocketBase>>> sockets,
    std::shared_ptr<ServerSocketFactory> socketFactory)
      : workers_(std::make_shared<WorkerMap>())
      , workersMutex_(std::make_shared<folly::SharedMutex>())
      , acceptorFactory_(acceptorFactory)
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
  using WorkerMap = std::map<wangle::ThreadPoolExecutor::ThreadHandle*,
        std::shared_ptr<Acceptor>>;
  std::shared_ptr<WorkerMap> workers_;
  std::shared_ptr<folly::SharedMutex> workersMutex_;
  std::shared_ptr<AcceptorFactory> acceptorFactory_;
  wangle::IOThreadPoolExecutor* exec_{nullptr};
  std::shared_ptr<std::vector<std::shared_ptr<folly::AsyncSocketBase>>>
      sockets_;
  std::shared_ptr<ServerSocketFactory> socketFactory_;
};

template <typename F>
void ServerWorkerPool::forEachWorker(F&& f) const {
  folly::SharedMutex::ReadHolder holder(workersMutex_.get());
  for (const auto& kv : *workers_) {
    f(kv.second.get());
  }
}

class DefaultAcceptPipelineFactory
    : public PipelineFactory<wangle::Pipeline<AcceptPipelineType>> {

 public:
  typename wangle::AcceptPipeline::Ptr newPipeline(
      std::shared_ptr<folly::AsyncSocket>) {
    return wangle::AcceptPipeline::create();
  }
};

} // namespace wangle

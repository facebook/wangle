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

#include <folly/ExceptionWrapper.h>
#include <folly/SharedMutex.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/EventBaseManager.h>
#include <wangle/acceptor/Acceptor.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/bootstrap/ServerSocketFactory.h>
#include <wangle/channel/Handler.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <wangle/ssl/SSLStats.h>

namespace wangle {

class AcceptorException : public std::runtime_error {
 public:
  enum class ExceptionType {
    UNKNOWN = 0,
    TIMED_OUT = 1,
    DROPPED = 2,
    ACCEPT_STOPPED = 3,
    FORCE_STOP = 4,
    INTERNAL_ERROR = 5,
  };

  explicit AcceptorException(ExceptionType type) :
      std::runtime_error(""), type_(type) {}

  AcceptorException(ExceptionType type, const std::string& message) :
      std::runtime_error(message), type_(type) {}

  ExceptionType getType() const noexcept { return type_; }

 protected:
  const ExceptionType type_;
};

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

    void timeoutExpired() noexcept override {
      auto ew = folly::make_exception_wrapper<AcceptorException>(
          AcceptorException::ExceptionType::TIMED_OUT, "timeout");
      pipeline_->readException(ew);
    }

    void describe(std::ostream& os) const override {}
    bool isBusy() const override {
      return true;
    }
    void notifyPendingShutdown() override {}
    void closeWhenIdle() override {}
    void dropConnection() override {
      DestructorGuard dg(this);
      auto ew = folly::make_exception_wrapper<AcceptorException>(
          AcceptorException::ExceptionType::DROPPED, "dropped");
      pipeline_->readException(ew);
      destroy();
    }
    void dumpConnectionState(uint8_t loglevel) override {}

    void deletePipeline(wangle::PipelineBase* p) override {
      CHECK(p == pipeline_.get());
      destroy();
    }

    void init() {
      pipeline_->transportActive();
    }

    void refreshTimeout() override {
      resetTimeout();
    }

   private:
    ~ServerConnection() {
      pipeline_->setPipelineManager(nullptr);
    }
    typename Pipeline::Ptr pipeline_;
  };

  explicit ServerAcceptor(
      std::shared_ptr<AcceptPipelineFactory> acceptPipelineFactory,
      std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory,
      const ServerSocketConfig& accConfig)
      : Acceptor(accConfig),
        acceptPipelineFactory_(acceptPipelineFactory),
        childPipelineFactory_(childPipelineFactory) {
  }

  void init(folly::AsyncServerSocket* serverSocket,
            folly::EventBase* eventBase,
            SSLStats* stats = nullptr) override {
    Acceptor::init(serverSocket, eventBase, stats);

    acceptPipeline_ = acceptPipelineFactory_->newPipeline(this);

    if (childPipelineFactory_) {
      // This means a custom AcceptPipelineFactory was not passed in via
      // pipeline() and we're using the DefaultAcceptPipelineFactory.
      // Add the default inbound handler here.
      acceptPipeline_->addBack(this);
    }
    acceptPipeline_->finalize();
  }

  void read(Context* ctx, AcceptPipelineType conn) override {
    if (conn.type() != typeid(ConnInfo&)) {
      return;
    }

    auto connInfo = boost::get<ConnInfo&>(conn);
    folly::AsyncTransportWrapper::UniquePtr transport(connInfo.sock);

    // Setup local and remote addresses
    auto tInfoPtr = std::make_shared<TransportInfo>(connInfo.tinfo);
    tInfoPtr->localAddr =
      std::make_shared<folly::SocketAddress>(accConfig_.bindAddress);
    transport->getLocalAddress(tInfoPtr->localAddr.get());
    tInfoPtr->remoteAddr =
      std::make_shared<folly::SocketAddress>(*connInfo.clientAddr);
    tInfoPtr->sslNextProtocol =
      std::make_shared<std::string>(connInfo.nextProtoName);

    auto pipeline = childPipelineFactory_->newPipeline(
      std::shared_ptr<folly::AsyncTransportWrapper>(
        transport.release(), folly::DelayedDestruction::Destructor()));
    pipeline->setTransportInfo(tInfoPtr);
    auto connection = new ServerConnection(std::move(pipeline));
    Acceptor::addConnection(connection);
    connection->init();
  }

  // Null implementation to terminate the call in this handler
  // and suppress warnings
  void readEOF(Context* ctx) override {}
  void readException(Context* ctx,
                     folly::exception_wrapper ex) override {}

  /* See Acceptor::onNewConnection for details */
  void onNewConnection(folly::AsyncTransportWrapper::UniquePtr transport,
                       const folly::SocketAddress* clientAddr,
                       const std::string& nextProtocolName,
                       SecureTransportType secureTransportType,
                       const TransportInfo& tinfo) override {
    ConnInfo connInfo = {transport.release(), clientAddr, nextProtocolName,
                         secureTransportType, tinfo};
    acceptPipeline_->read(connInfo);
  }

  // notify the acceptors in the acceptPipeline to drain & drop conns
  void acceptStopped() noexcept override {
    auto ew = folly::make_exception_wrapper<AcceptorException>(
      AcceptorException::ExceptionType::ACCEPT_STOPPED,
      "graceful shutdown timeout");

    acceptPipeline_->readException(ew);
    Acceptor::acceptStopped();
  }

  void forceStop() noexcept override {
    auto ew = folly::make_exception_wrapper<AcceptorException>(
      AcceptorException::ExceptionType::FORCE_STOP,
      "hard shutdown timeout");

    acceptPipeline_->readException(ew);
    Acceptor::forceStop();
  }

  // UDP thunk
  void onDataAvailable(std::shared_ptr<folly::AsyncUDPSocket> socket,
                       const folly::SocketAddress& addr,
                       std::unique_ptr<folly::IOBuf> buf,
                       bool truncated) noexcept override {
    acceptPipeline_->read(
        AcceptPipelineType(make_tuple(buf.release(), socket, addr)));
  }

  void onConnectionAdded(const wangle::ConnectionManager&) override {
    acceptPipeline_->read(ConnEvent::CONN_ADDED);
  }

  void onConnectionRemoved(const wangle::ConnectionManager&) override {
    acceptPipeline_->read(ConnEvent::CONN_REMOVED);
  }

  void sslConnectionError(const folly::exception_wrapper& ex) override {
    acceptPipeline_->readException(ex);
    Acceptor::sslConnectionError(ex);
  }

 private:
  std::shared_ptr<AcceptPipelineFactory> acceptPipelineFactory_;
  std::shared_ptr<AcceptPipeline> acceptPipeline_;
  std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory_;
};

template <typename Pipeline>
class ServerAcceptorFactory : public AcceptorFactory {
 public:
  explicit ServerAcceptorFactory(
      std::shared_ptr<AcceptPipelineFactory> acceptPipelineFactory,
      std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory,
      const ServerSocketConfig& accConfig)
      : acceptPipelineFactory_(acceptPipelineFactory),
        childPipelineFactory_(childPipelineFactory),
        accConfig_(accConfig) {}

  std::shared_ptr<Acceptor> newAcceptor(folly::EventBase* base) {
    auto acceptor = std::make_shared<ServerAcceptor<Pipeline>>(
        acceptPipelineFactory_, childPipelineFactory_, accConfig_);
    acceptor->init(nullptr, base, nullptr);
    return acceptor;
  }

 private:
  std::shared_ptr<AcceptPipelineFactory> acceptPipelineFactory_;
  std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory_;
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
      , workersMutex_(std::make_shared<Mutex>())
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
  using Mutex = folly::SharedMutexReadPriority;

  std::shared_ptr<WorkerMap> workers_;
  std::shared_ptr<Mutex> workersMutex_;
  std::shared_ptr<AcceptorFactory> acceptorFactory_;
  wangle::IOThreadPoolExecutor* exec_{nullptr};
  std::shared_ptr<std::vector<std::shared_ptr<folly::AsyncSocketBase>>>
      sockets_;
  std::shared_ptr<ServerSocketFactory> socketFactory_;
};

template <typename F>
void ServerWorkerPool::forEachWorker(F&& f) const {
  Mutex::ReadHolder holder(workersMutex_.get());
  for (const auto& kv : *workers_) {
    f(kv.second.get());
  }
}

class DefaultAcceptPipelineFactory : public AcceptPipelineFactory {
 public:
  typename AcceptPipeline::Ptr newPipeline(Acceptor* acceptor) {
    return AcceptPipeline::create();
  }
};

} // namespace wangle

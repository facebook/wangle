// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <folly/ThreadLocal.h>
#include <wangle/bootstrap/AcceptRoutingHandler.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/broadcast/BroadcastPool.h>
#include <wangle/channel/broadcast/Subscriber.h>

namespace folly { namespace wangle {

/**
 * A Handler-Observer adaptor that can be used for subscribing to broadcasts.
 * Maintains a thread-local BroadcastPool from which a BroadcastHandler is
 * obtained and subscribed to based on the given routing data.
 *
 * If custom logic needs to be added based on inbound bytes, subclass and
 * override the handler's read() method.
 */
template <typename R>
class ObservingHandler : public BytesToBytesHandler,
                         public Subscriber<std::unique_ptr<folly::IOBuf>> {
 public:
  ObservingHandler(
      const R& routingData,
      std::shared_ptr<ServerPool> serverPool,
      std::shared_ptr<BroadcastHandlerFactory<std::unique_ptr<folly::IOBuf>>>
          broadcastHandlerFactory)
      : routingData_(routingData),
        serverPool_(serverPool),
        broadcastHandlerFactory_(broadcastHandlerFactory) {}

  virtual ~ObservingHandler() {
    CHECK(!broadcastHandler_);
  }

  // BytesToBytesHandler implementation
  void transportActive(Context* ctx) override;
  void readEOF(Context* ctx) override;
  void readException(Context* ctx, folly::exception_wrapper ex) override;

  // Subscriber implementation
  void onNext(const std::unique_ptr<folly::IOBuf>& buf) override;
  void onError(folly::exception_wrapper ex) override;
  void onCompleted() override;

  /**
   * Pause listening to the broadcast.
   * All bytes streamed from the broadcast will be dropped.
   */
  void pause() noexcept { paused_ = true; }

  /**
   * Resume listening to the braodcast if paused.
   */
  void resume() noexcept { paused_ = false; }

 protected:
  /**
   * Unsubscribe from the broadcast and close the handler.
   */
  void closeHandler();

 private:
  /**
   * Lazily initialize and return a thread-local BroadcastPool.
   */
  BroadcastPool<std::unique_ptr<folly::IOBuf>, R>* broadcastPool();

  // For testing
  virtual BroadcastPool<std::unique_ptr<folly::IOBuf>, R>* newBroadcastPool();

  R routingData_;
  std::shared_ptr<ServerPool> serverPool_;
  std::shared_ptr<BroadcastHandlerFactory<std::unique_ptr<folly::IOBuf>>>
      broadcastHandlerFactory_;

  BroadcastHandler<std::unique_ptr<folly::IOBuf>>* broadcastHandler_{nullptr};
  uint64_t subscriptionId_{0};
  bool paused_{false};

  folly::ThreadLocalPtr<BroadcastPool<std::unique_ptr<folly::IOBuf>, R>>
      broadcastPool_;
};

template <typename R>
class ObservingPipelineFactory
    : public RoutingDataPipelineFactory<DefaultPipeline, R> {
 public:
  ObservingPipelineFactory(
      std::shared_ptr<ServerPool> serverPool,
      std::shared_ptr<BroadcastHandlerFactory<std::unique_ptr<folly::IOBuf>>>
          broadcastHandlerFactory)
      : serverPool_(serverPool),
        broadcastHandlerFactory_(broadcastHandlerFactory) {}

  DefaultPipeline::UniquePtr newPipeline(
      std::shared_ptr<folly::AsyncSocket> socket,
      const R& routingData) override {
    DefaultPipeline::UniquePtr pipeline(new DefaultPipeline);
    pipeline->addBack(AsyncSocketHandler(socket));
    auto handler = std::make_shared<ObservingHandler<R>>(
        routingData, serverPool_, broadcastHandlerFactory_);
    pipeline->addBack(handler);
    pipeline->finalize();

    return pipeline;
  }

 protected:
  std::shared_ptr<ServerPool> serverPool_;
  std::shared_ptr<BroadcastHandlerFactory<std::unique_ptr<folly::IOBuf>>>
      broadcastHandlerFactory_;
};

}} // namespace folly::wangle

#include <wangle/channel/broadcast/ObservingHandler-inl.h>

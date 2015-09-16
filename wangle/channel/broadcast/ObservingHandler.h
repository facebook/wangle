// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <wangle/bootstrap/AcceptRoutingHandler.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/broadcast/BroadcastPool.h>
#include <wangle/channel/broadcast/Subscriber.h>

namespace wangle {

/**
 * A Handler-Observer adaptor that can be used for subscribing to broadcasts.
 * Maintains a thread-local BroadcastPool from which a BroadcastHandler is
 * obtained and subscribed to based on the given routing data.
 */
template <typename T, typename R>
class ObservingHandler : public HandlerAdapter<folly::IOBufQueue&, T>,
                         public Subscriber<T> {
 public:
  typedef typename HandlerAdapter<folly::IOBufQueue&, T>::Context Context;

  ObservingHandler(
      const R& routingData,
      std::shared_ptr<ServerPool<R>> serverPool,
      std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory)
      : routingData_(routingData),
        serverPool_(serverPool),
        broadcastPipelineFactory_(broadcastPipelineFactory) {}

  virtual ~ObservingHandler() {
    CHECK(!broadcastHandler_);
  }

  // HandlerAdapter implementation
  void transportActive(Context* ctx) override;
  void readEOF(Context* ctx) override;
  void readException(Context* ctx, folly::exception_wrapper ex) override;

  // Subscriber implementation
  void onNext(const T& buf) override;
  void onError(folly::exception_wrapper ex) override;
  void onCompleted() override;

 protected:
  /**
   * Unsubscribe from the broadcast and close the handler.
   */
  void closeHandler();

 private:
  // For testing
  virtual BroadcastPool<T, R>* broadcastPool();

  R routingData_;
  std::shared_ptr<ServerPool<R>> serverPool_;
  std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory_;

  BroadcastHandler<T>* broadcastHandler_{nullptr};
  uint64_t subscriptionId_{0};
  bool paused_{false};
};

template <typename T>
using ObservingPipeline = Pipeline<folly::IOBufQueue&, T>;

template <typename T, typename R>
class ObservingPipelineFactory
    : public RoutingDataPipelineFactory<ObservingPipeline<T>, R> {
 public:
  ObservingPipelineFactory(
      std::shared_ptr<ServerPool<R>> serverPool,
      std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory)
      : serverPool_(serverPool),
        broadcastPipelineFactory_(broadcastPipelineFactory) {}

  typename ObservingPipeline<T>::Ptr newPipeline(
      std::shared_ptr<folly::AsyncSocket> socket,
      const R& routingData) override {
    auto pipeline = ObservingPipeline<T>::create();
    pipeline->addBack(AsyncSocketHandler(socket));
    auto handler = std::make_shared<ObservingHandler<T, R>>(
        routingData, serverPool_, broadcastPipelineFactory_);
    pipeline->addBack(handler);
    pipeline->finalize();

    return pipeline;
  }

 protected:
  std::shared_ptr<ServerPool<R>> serverPool_;
  std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory_;
};

} // namespace wangle

#include <wangle/channel/broadcast/ObservingHandler-inl.h>

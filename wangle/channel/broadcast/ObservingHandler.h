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
                         public Subscriber<T, R> {
 public:
  typedef typename HandlerAdapter<folly::IOBufQueue&, T>::Context Context;

  ObservingHandler(const R& routingData, BroadcastPool<T, R>* broadcastPool);
  ~ObservingHandler() override;

  // Non-copyable
  ObservingHandler(const ObservingHandler&) = delete;
  ObservingHandler& operator=(const ObservingHandler&) = delete;

  // Movable
  ObservingHandler(ObservingHandler&&) = default;
  ObservingHandler& operator=(ObservingHandler&&) = default;

  // HandlerAdapter implementation
  void transportActive(Context* ctx) override;
  void readEOF(Context* ctx) override;
  void readException(Context* ctx, folly::exception_wrapper ex) override;

  // Subscriber implementation
  void onNext(const T& buf) override;
  void onError(folly::exception_wrapper ex) override;
  void onCompleted() override;
  R& routingData() override;

 private:
  R routingData_;
  BroadcastPool<T, R>* broadcastPool_{nullptr};

  BroadcastHandler<T, R>* broadcastHandler_{nullptr};
  uint64_t subscriptionId_{0};
  bool paused_{false};

  // True iff the handler has been deleted
  std::shared_ptr<bool> deleted_{new bool(false)};
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
      const R& routingData,
      RoutingDataHandler<R>* routingHandler,
      std::shared_ptr<TransportInfo> transportInfo) override {
    auto pipeline = ObservingPipeline<T>::create();
    pipeline->addBack(AsyncSocketHandler(socket));
    auto handler =
        std::make_shared<ObservingHandler<T, R>>(routingData, broadcastPool());
    pipeline->addBack(handler);
    pipeline->finalize();

    pipeline->setTransportInfo(transportInfo);

    return pipeline;
  }

  virtual BroadcastPool<T, R>* broadcastPool() {
    if (!broadcastPool_) {
      broadcastPool_.reset(
          new BroadcastPool<T, R>(serverPool_, broadcastPipelineFactory_));
    }
    return broadcastPool_.get();
  }

 protected:
  std::shared_ptr<ServerPool<R>> serverPool_;
  std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory_;
  folly::ThreadLocalPtr<BroadcastPool<T, R>> broadcastPool_;
};

} // namespace wangle

#include <wangle/channel/broadcast/ObservingHandler-inl.h>

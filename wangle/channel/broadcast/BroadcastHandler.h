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

#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/Handler.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/broadcast/Subscriber.h>

namespace wangle {

/**
 * An Observable type handler for broadcasting/streaming data to a list
 * of subscribers.
 */
template <typename T, typename R>
class BroadcastHandler : public HandlerAdapter<T, std::unique_ptr<folly::IOBuf>> {
 public:
  typedef typename HandlerAdapter<T, std::unique_ptr<folly::IOBuf>>::Context Context;

  virtual ~BroadcastHandler() {
    CHECK(subscribers_.empty());
  }

  // BytesToBytesHandler implementation
  void read(Context* ctx, T data) override;
  void readEOF(Context* ctx) override;
  void readException(Context* ctx, folly::exception_wrapper ex) override;

  /**
   * Subscribes to the broadcast. Returns a unique subscription ID
   * for this subscriber.
   */
  virtual uint64_t subscribe(Subscriber<T, R>* subscriber);

  /**
   * Unsubscribe from the broadcast. Closes the pipeline if the
   * number of subscribers reaches zero.
   */
  virtual void unsubscribe(uint64_t subscriptionId);

  /**
   * If there are no subscribers listening to the broadcast, close the pipeline.
   * This will also delete the broadcast from the BroadcastPool.
   */
  virtual void closeIfIdle();

  /**
   * Invoked when a new subscriber is added. Subclasses can override
   * to add custom behavior.
   */
  virtual void onSubscribe(Subscriber<T, R>* subscriber) {}

  /**
   * Invoked when a subscriber is removed. Subclasses can override
   * to add custom behavior.
   */
  virtual void onUnsubscribe(Subscriber<T, R>* subscriber) {}

  /**
   * Invoked for each data that is about to be broadcasted to the
   * subscribers. Subclasses can override to add custom behavior.
   */
  virtual void onData(T& data) {}

 protected:
  template <typename FUNC> // FUNC: Subscriber<T, R>* -> void
  void forEachSubscriber(FUNC f) {
    auto subscribers = subscribers_;
    for (const auto& it : subscribers) {
      f(it.second);
    }
  }

 private:
  std::map<uint64_t, Subscriber<T, R>*> subscribers_;
  uint64_t nextSubscriptionId_{0};
};

template <typename T, typename R>
class BroadcastPipelineFactory
    : public PipelineFactory<DefaultPipeline> {
 public:
  virtual DefaultPipeline::Ptr newPipeline(
      std::shared_ptr<folly::AsyncTransportWrapper> socket) override = 0;

  virtual BroadcastHandler<T, R>* getBroadcastHandler(
      DefaultPipeline* pipeline) noexcept = 0;

  virtual void setRoutingData(DefaultPipeline* pipeline,
                              const R& routingData) = 0;
};

} // namespace wangle

#include <wangle/channel/broadcast/BroadcastHandler-inl.h>

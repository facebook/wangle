// Copyright 2004-present Facebook.  All rights reserved.
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
template <typename T>
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
  virtual uint64_t subscribe(Subscriber<T>* subscriber);

  /**
   * Unsubscribe from the broadcast. Closes the pipeline if the
   * number of subscribers reaches zero.
   */
  virtual void unsubscribe(uint64_t subscriptionId);

 protected:
  template <typename FUNC> // FUNC: Subscriber<T>* -> void
  void forEachSubscriber(FUNC f) {
    auto subscribers = subscribers_;
    for (const auto& it : subscribers) {
      f(it.second);
    }
  }

 private:
  std::map<uint64_t, Subscriber<T>*> subscribers_;
  uint64_t nextSubscriptionId_{0};
};

template <typename T>
class BroadcastHandlerFactory {
 public:
  virtual ~BroadcastHandlerFactory() {}

  virtual std::shared_ptr<BroadcastHandler<T>> newHandler() = 0;
};

template <typename T, typename R>
class BroadcastPipelineFactory
    : public PipelineFactory<DefaultPipeline> {
 public:
  virtual DefaultPipeline::UniquePtr newPipeline(
      std::shared_ptr<folly::AsyncSocket> socket) override = 0;

  virtual BroadcastHandler<T>* getBroadcastHandler(
      DefaultPipeline* pipeline) noexcept = 0;

  virtual void setRoutingData(DefaultPipeline* pipeline,
                              const R& routingData) noexcept = 0;
};

} // namespace wangle

#include <wangle/channel/broadcast/BroadcastHandler-inl.h>

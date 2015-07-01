// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/Handler.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/broadcast/Subscriber.h>

namespace folly { namespace wangle {

/**
 * An Observable type handler for broadcasting/streaming data to a list
 * of subscribers.
 */
template <typename T>
class BroadcastHandler : public BytesToBytesHandler {
 public:
  virtual ~BroadcastHandler() {
    CHECK(subscribers_.empty());
  }

  // BytesToBytesHandler implementation
  void read(Context* ctx, folly::IOBufQueue& q) override;
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

  /**
   * Process bytes read from the input IOBufQueue and store
   * it in `data` for broadcasting it to the subscribers.
   *
   * @return bool   True if `data` is ready to be broadcast,
   *                false if waiting for more bytes.
   */
  virtual bool processRead(folly::IOBufQueue& q, T& data) = 0;

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

template <typename T>
class BroadcastPipelineFactory
    : public folly::PipelineFactory<DefaultPipeline> {
 public:
  explicit BroadcastPipelineFactory(
      std::shared_ptr<BroadcastHandlerFactory<T>> handlerFactory)
      : handlerFactory_(handlerFactory) {}

  DefaultPipeline::UniquePtr newPipeline(
      std::shared_ptr<folly::AsyncSocket> socket) override {
    DefaultPipeline::UniquePtr pipeline(new DefaultPipeline);
    pipeline->addBack(AsyncSocketHandler(socket));
    pipeline->addBack(handlerFactory_->newHandler());
    pipeline->finalize();

    return pipeline;
  }

  static BroadcastHandler<T>* getBroadcastHandler(DefaultPipeline* pipeline) {
    DCHECK(pipeline);
    return pipeline->getHandler<BroadcastHandler<T>>(1);
  }

 private:
  std::shared_ptr<BroadcastHandlerFactory<T>> handlerFactory_;
};

}} // namespace folly::wangle

#include <wangle/channel/broadcast/BroadcastHandler-inl.h>

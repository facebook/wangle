// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

namespace folly { namespace wangle {

template <typename R>
void ObservingHandler<R>::transportActive(Context* ctx) {
  if (broadcastHandler_) {
    // Already connected
    return;
  }

  // Pause ingress until the remote connection is established and
  // broadcast handler is ready
  auto pipeline = dynamic_cast<DefaultPipeline*>(ctx->getPipeline());
  CHECK(pipeline);
  pipeline->transportInactive();

  auto pool = broadcastPool();
  pool->getHandler(routingData_)
      .then([this, pipeline](
          BroadcastHandler<std::unique_ptr<folly::IOBuf>>* broadcastHandler) {
        broadcastHandler_ = broadcastHandler;
        subscriptionId_ = broadcastHandler_->subscribe(this);
        VLOG(10) << "Subscribed to a broadcast";

        // Resume ingress
        pipeline->transportActive();
      })
      .onError([this, ctx](const std::exception& ex) {
        LOG(ERROR) << "Error subscribing to a broadcast: " << ex.what();
        closeHandler();
      });
}

template <typename R>
void ObservingHandler<R>::readEOF(Context* ctx) {
  closeHandler();
}

template <typename R>
void ObservingHandler<R>::readException(Context* ctx,
                                        folly::exception_wrapper ex) {
  LOG(ERROR) << "Error on read: " << exceptionStr(ex);
  closeHandler();
}

template <typename R>
void ObservingHandler<R>::onNext(const std::unique_ptr<folly::IOBuf>& buf) {
  if (!buf || paused_) {
    return;
  }

  write(getContext(), buf->clone())
      .onError([this](const std::exception& ex) {
        LOG(ERROR) << "Error on write: " << ex.what();
        closeHandler();
      });
}

template <typename R>
void ObservingHandler<R>::onError(folly::exception_wrapper ex) {
  LOG(ERROR) << "Error observing a broadcast: " << exceptionStr(ex);

  // broadcastHandler_ will clear its subscribers and delete itself
  broadcastHandler_ = nullptr;
  closeHandler();
}

template <typename R>
void ObservingHandler<R>::onCompleted() {
  // broadcastHandler_ will clear its subscribers and delete itself
  broadcastHandler_ = nullptr;
  closeHandler();
}

template <typename R>
void ObservingHandler<R>::closeHandler() {
  if (broadcastHandler_) {
    auto broadcastHandler = broadcastHandler_;
    broadcastHandler_ = nullptr;
    broadcastHandler->unsubscribe(subscriptionId_);
  }
  close(getContext());
}

template <typename R>
BroadcastPool<std::unique_ptr<folly::IOBuf>, R>*
ObservingHandler<R>::broadcastPool() {
  if (!broadcastPool_) {
    broadcastPool_.reset(newBroadcastPool());
  }
  return broadcastPool_.get();
}

template <typename R>
BroadcastPool<std::unique_ptr<folly::IOBuf>, R>*
ObservingHandler<R>::newBroadcastPool() {
  return (new BroadcastPool<std::unique_ptr<folly::IOBuf>, R>(
      serverPool_, broadcastPipelineFactory_));
}

}} // namespace folly::wangle

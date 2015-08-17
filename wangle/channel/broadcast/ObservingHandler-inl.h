// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

namespace wangle {

template <typename T, typename R>
void ObservingHandler<T, R>::transportActive(Context* ctx) {
  if (broadcastHandler_) {
    // Already connected
    return;
  }

  // Pause ingress until the remote connection is established and
  // broadcast handler is ready
  auto pipeline = dynamic_cast<ObservingPipeline<T>*>(ctx->getPipeline());
  CHECK(pipeline);
  pipeline->transportInactive();

  auto pool = broadcastPool();
  pool->getHandler(routingData_)
      .then([this, pipeline](BroadcastHandler<T>* broadcastHandler) {
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

template <typename T, typename R>
void ObservingHandler<T, R>::readEOF(Context* ctx) {
  closeHandler();
}

template <typename T, typename R>
void ObservingHandler<T, R>::readException(Context* ctx,
                                        folly::exception_wrapper ex) {
  LOG(ERROR) << "Error on read: " << exceptionStr(ex);
  closeHandler();
}

template <typename T, typename R>
void ObservingHandler<T, R>::onNext(const T& data) {
  this->write(this->getContext(), data)
      .onError([this](const std::exception& ex) {
        LOG(ERROR) << "Error on write: " << ex.what();
        closeHandler();
      });
}

template <typename T, typename R>
void ObservingHandler<T, R>::onError(folly::exception_wrapper ex) {
  LOG(ERROR) << "Error observing a broadcast: " << exceptionStr(ex);

  // broadcastHandler_ will clear its subscribers and delete itself
  broadcastHandler_ = nullptr;
  closeHandler();
}

template <typename T, typename R>
void ObservingHandler<T, R>::onCompleted() {
  // broadcastHandler_ will clear its subscribers and delete itself
  broadcastHandler_ = nullptr;
  closeHandler();
}

template <typename T, typename R>
void ObservingHandler<T, R>::closeHandler() {
  if (broadcastHandler_) {
    auto broadcastHandler = broadcastHandler_;
    broadcastHandler_ = nullptr;
    broadcastHandler->unsubscribe(subscriptionId_);
  }
  this->close(this->getContext());
}

template <typename T, typename R>
BroadcastPool<T, R>* ObservingHandler<T, R>::broadcastPool() {
  if (!broadcastPool_) {
    broadcastPool_.reset(newBroadcastPool());
  }
  return broadcastPool_.get();
}

template <typename T, typename R>
BroadcastPool<T, R>* ObservingHandler<T, R>::newBroadcastPool() {
  return (new BroadcastPool<T, R>(serverPool_, broadcastPipelineFactory_));
}

} // namespace wangle

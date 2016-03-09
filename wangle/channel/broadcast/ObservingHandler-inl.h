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

namespace wangle {

template <typename T, typename R>
ObservingHandler<T, R>::ObservingHandler(const R& routingData,
                                         BroadcastPool<T, R>* broadcastPool)
    : routingData_(routingData), broadcastPool_(CHECK_NOTNULL(broadcastPool)) {}

template <typename T, typename R>
ObservingHandler<T, R>::~ObservingHandler() {
  if (broadcastHandler_) {
    auto broadcastHandler = broadcastHandler_;
    broadcastHandler_ = nullptr;
    broadcastHandler->unsubscribe(subscriptionId_);
  }

  if (deleted_) {
    *deleted_ = true;
  }
}

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

  auto deleted = deleted_;
  broadcastPool_->getHandler(routingData_)
      .then(
           [this, pipeline, deleted](BroadcastHandler<T, R>* broadcastHandler) {
             if (*deleted) {
               return;
             }

             broadcastHandler_ = broadcastHandler;
             subscriptionId_ = broadcastHandler_->subscribe(this);
             VLOG(10) << "Subscribed to a broadcast";

             // Resume ingress
             pipeline->transportActive();
           })
      .onError([this, ctx, deleted](const std::exception& ex) {
        if (*deleted) {
          return;
        }

        LOG(ERROR) << "Error subscribing to a broadcast: " << ex.what();
        this->close(ctx);
      });
}

template <typename T, typename R>
void ObservingHandler<T, R>::readEOF(Context* ctx) {
  this->close(ctx);
}

template <typename T, typename R>
void ObservingHandler<T, R>::readException(Context* ctx,
                                           folly::exception_wrapper ex) {
  LOG(ERROR) << "Error on read: " << exceptionStr(ex);
  this->close(ctx);
}

template <typename T, typename R>
void ObservingHandler<T, R>::onNext(const T& data) {
  auto ctx = this->getContext();
  auto deleted = deleted_;
  this->write(ctx, data)
      .onError([this, ctx, deleted](const std::exception& ex) {
        if (*deleted) {
          return;
        }

        LOG(ERROR) << "Error on write: " << ex.what();
        this->close(ctx);
      });
}

template <typename T, typename R>
void ObservingHandler<T, R>::onError(folly::exception_wrapper ex) {
  LOG(ERROR) << "Error observing a broadcast: " << exceptionStr(ex);

  // broadcastHandler_ will clear its subscribers and delete itself
  broadcastHandler_ = nullptr;
  this->close(this->getContext());
}

template <typename T, typename R>
void ObservingHandler<T, R>::onCompleted() {
  // broadcastHandler_ will clear its subscribers and delete itself
  broadcastHandler_ = nullptr;
  this->close(this->getContext());
}

template <typename T, typename R>
R& ObservingHandler<T, R>::routingData() {
  return routingData_;
}

} // namespace wangle

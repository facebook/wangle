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
void BroadcastHandler<T, R>::read(Context* ctx, T data) {
  onData(data);
  forEachSubscriber([&](Subscriber<T, R>* s) {
    s->onNext(data);
  });
}

template <typename T, typename R>
void BroadcastHandler<T, R>::readEOF(Context* ctx) {
  forEachSubscriber([&](Subscriber<T, R>* s) {
    s->onCompleted();
  });
  subscribers_.clear();
  closeIfIdle();
}

template <typename T, typename R>
void BroadcastHandler<T, R>::readException(Context* ctx,
                                        folly::exception_wrapper ex) {
  LOG(ERROR) << "Error while reading from upstream for broadcast: "
             << exceptionStr(ex);

  forEachSubscriber([&](Subscriber<T, R>* s) {
    s->onError(ex);
  });
  subscribers_.clear();
  closeIfIdle();
}

template <typename T, typename R>
uint64_t BroadcastHandler<T, R>::subscribe(Subscriber<T, R>* subscriber) {
  auto subscriptionId = nextSubscriptionId_++;
  subscribers_[subscriptionId] = subscriber;
  onSubscribe(subscriber);
  return subscriptionId;
}

template <typename T, typename R>
void BroadcastHandler<T, R>::unsubscribe(uint64_t subscriptionId) {
  auto iter = subscribers_.find(subscriptionId);
  if (iter == subscribers_.end()) {
    return;
  }

  onUnsubscribe(iter->second);
  subscribers_.erase(iter);
  closeIfIdle();
}

template <typename T, typename R>
void BroadcastHandler<T, R>::closeIfIdle() {
  if (subscribers_.empty()) {
    // No more subscribers. Clean up.
    // This will delete the broadcast from the pool.
    this->close(this->getContext());
  }
}

} // namespace wangle

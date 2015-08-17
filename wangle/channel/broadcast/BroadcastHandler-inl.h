// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

namespace wangle {

template <typename T>
void BroadcastHandler<T>::read(Context* ctx, T data) {
  forEachSubscriber([&](Subscriber<T>* s) {
    s->onNext(data);
  });
}

template <typename T>
void BroadcastHandler<T>::readEOF(Context* ctx) {
  forEachSubscriber([&](Subscriber<T>* s) {
    s->onCompleted();
  });
  subscribers_.clear();

  // This will delete the broadcast from the pool
  this->close(ctx);
}

template <typename T>
void BroadcastHandler<T>::readException(Context* ctx,
                                        folly::exception_wrapper ex) {
  LOG(ERROR) << "Error while reading from upstream for broadcast: "
             << exceptionStr(ex);

  forEachSubscriber([&](Subscriber<T>* s) {
    s->onError(ex);
  });
  subscribers_.clear();

  // This will delete the broadcast from the pool
  this->close(ctx);
}

template <typename T>
uint64_t BroadcastHandler<T>::subscribe(Subscriber<T>* subscriber) {
  auto subscriptionId = nextSubscriptionId_++;
  subscribers_[subscriptionId] = subscriber;
  return subscriptionId;
}

template <typename T>
void BroadcastHandler<T>::unsubscribe(uint64_t subscriptionId) {
  subscribers_.erase(subscriptionId);
  if (subscribers_.empty()) {
    // No more subscribers. Clean up.
    // This will delete the broadcast from the pool.
    this->close(this->getContext());
  }
}

} // namespace wangle

// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

namespace wangle {

template <typename T, typename R>
folly::Future<BroadcastHandler<T>*>
BroadcastPool<T, R>::BroadcastManager::getHandler() {
  // getFuture() returns a completed future if we are already connected
  auto future = sharedPromise_.getFuture();

  if (connectStarted_) {
    // Either already connected, in which case the future has the handler,
    // or there's an outstanding connect request and the promise will be
    // fulfilled when the connect request completes.
    return future;
  }

  // Kickoff connect request and fulfill all pending promises on completion
  connectStarted_ = true;
  const auto& addr = pool_->getServer();
  client_.connect(addr)
      .then([this](DefaultPipeline* pipeline) {
        pipeline->setPipelineManager(this);

        broadcastPipelineFactory_->setRoutingData(pipeline, routingData_);

        auto handler = broadcastPipelineFactory_->getBroadcastHandler(pipeline);
        CHECK(handler);
        sharedPromise_.setValue(handler);
      })
      .onError([this](const std::exception& ex) {
        LOG(ERROR) << "Connect error: " << ex.what();
        auto ew = folly::make_exception_wrapper<std::exception>(ex);

        // Delete the broadcast before fulfilling the promises as the
        // futures' onError callbacks can delete the pool
        auto sharedPromise = std::move(sharedPromise_);
        pool_->deleteBroadcast(routingData_);
        sharedPromise.setException(ew);
      });

  return future;
}

template <typename T, typename R>
folly::Future<BroadcastHandler<T>*> BroadcastPool<T, R>::getHandler(
    const R& routingData) {
  const auto& iter = broadcasts_.find(routingData);
  if (iter != broadcasts_.end()) {
    return iter->second->getHandler();
  }

  auto broadcast = folly::make_unique<BroadcastManager>(
      this, routingData, broadcastPipelineFactory_);
  auto broadcastPtr = broadcast.get();
  broadcasts_.insert(std::make_pair(routingData, std::move(broadcast)));

  return broadcastPtr->getHandler();
}

} // namespace wangle

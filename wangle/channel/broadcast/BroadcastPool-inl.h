// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

namespace wangle {

template <typename T, typename R>
folly::ThreadLocalPtr<BroadcastPool<T, R>> BroadcastPool<T, R>::instance_;

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

  broadcastPool_->serverPool_->connect(&client_, routingData_)
      .then([this](DefaultPipeline* pipeline) {
        pipeline->setPipelineManager(this);

        auto pipelineFactory = broadcastPool_->broadcastPipelineFactory_;
        pipelineFactory->setRoutingData(pipeline, routingData_);

        auto handler = pipelineFactory->getBroadcastHandler(pipeline);
        CHECK(handler);
        sharedPromise_.setValue(handler);
      })
      .onError([this](const std::exception& ex) {
        LOG(ERROR) << "Connect error: " << ex.what();
        auto ew = folly::make_exception_wrapper<std::exception>(ex);

        auto sharedPromise = std::move(sharedPromise_);
        broadcastPool_->deleteBroadcast(routingData_);
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

  auto broadcast = folly::make_unique<BroadcastManager>(this, routingData);
  auto broadcastPtr = broadcast.get();
  broadcasts_.insert(std::make_pair(routingData, std::move(broadcast)));

  return broadcastPtr->getHandler();
}

} // namespace wangle

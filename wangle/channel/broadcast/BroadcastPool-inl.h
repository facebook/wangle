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
folly::Future<BroadcastHandler<T, R>*>
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
        try {
          pipelineFactory->setRoutingData(pipeline, routingData_);
        } catch (const std::exception& ex) {
          handleConnectError(ex);
          return;
        }

        auto handler = pipelineFactory->getBroadcastHandler(pipeline);
        CHECK(handler);
        sharedPromise_.setValue(handler);

        // If all the observers go away before connect returns, then the
        // BroadcastHandler will be idle without any subscribers. Close
        // the pipeline and remove the broadcast from the pool so that
        // connections are not leaked.
        handler->closeIfIdle();
      })
      .onError([this](const std::exception& ex) {
        handleConnectError(ex);
      });

  return future;
}

template <typename T, typename R>
void BroadcastPool<T, R>::BroadcastManager::deletePipeline(
    PipelineBase* pipeline) {
  CHECK(client_.getPipeline() == pipeline);
  broadcastPool_->deleteBroadcast(routingData_);
}

template <typename T, typename R>
void BroadcastPool<T, R>::BroadcastManager::handleConnectError(
    const std::exception& ex) noexcept {
  LOG(ERROR) << "Error connecting to upstream: " << ex.what();

  auto sharedPromise = std::move(sharedPromise_);
  broadcastPool_->deleteBroadcast(routingData_);
  sharedPromise.setException(folly::make_exception_wrapper<std::exception>(ex));
}

template <typename T, typename R>
folly::Future<BroadcastHandler<T, R>*> BroadcastPool<T, R>::getHandler(
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

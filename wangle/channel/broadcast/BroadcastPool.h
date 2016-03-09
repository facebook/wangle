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

#include <folly/ThreadLocal.h>
#include <folly/futures/SharedPromise.h>
#include <wangle/bootstrap/ClientBootstrap.h>
#include <wangle/channel/broadcast/BroadcastHandler.h>

namespace wangle {

template <typename R>
class ServerPool {
 public:
  virtual ~ServerPool() {}

  /**
   * Kick off an upstream connect request given the ClientBootstrap
   * when a broadcast is not available locally.
   */
  virtual folly::Future<DefaultPipeline*> connect(
      ClientBootstrap<DefaultPipeline>* client,
      const R& routingData) noexcept = 0;
};

/**
 * A pool of upstream broadcast pipelines. There is atmost one broadcast
 * for any unique routing data. Creates and maintains upstream connections
 * and broadcast pipeliens as necessary.
 *
 * Meant to be used as a thread-local instance.
 */
template <typename T, typename R>
class BroadcastPool {
 public:
  class BroadcastManager : PipelineManager {
   public:
    BroadcastManager(BroadcastPool<T, R>* broadcastPool, const R& routingData)
        : broadcastPool_(broadcastPool), routingData_(routingData) {
      client_.pipelineFactory(broadcastPool_->broadcastPipelineFactory_);
    }

    virtual ~BroadcastManager() {
      if (client_.getPipeline()) {
        client_.getPipeline()->setPipelineManager(nullptr);
      }
    }

    folly::Future<BroadcastHandler<T, R>*> getHandler();

    // PipelineManager implementation
    void deletePipeline(PipelineBase* pipeline) override;

   private:
    void handleConnectError(const std::exception& ex) noexcept;

    BroadcastPool<T, R>* broadcastPool_{nullptr};
    R routingData_;
    ClientBootstrap<DefaultPipeline> client_;

    bool connectStarted_{false};
    folly::SharedPromise<BroadcastHandler<T, R>*> sharedPromise_;
  };

  BroadcastPool(std::shared_ptr<ServerPool<R>> serverPool,
                std::shared_ptr<BroadcastPipelineFactory<T, R>> pipelineFactory)
      : serverPool_(serverPool), broadcastPipelineFactory_(pipelineFactory) {}

  virtual ~BroadcastPool() {}

  // Non-copyable
  BroadcastPool(const BroadcastPool&) = delete;
  BroadcastPool& operator=(const BroadcastPool&) = delete;

  // Movable
  BroadcastPool(BroadcastPool&&) = default;
  BroadcastPool& operator=(BroadcastPool&&) = default;

  /**
   * Gets the BroadcastHandler, or creates one if it doesn't exist already,
   * for the given routingData.
   *
   * If a broadcast is already available for the given routingData,
   * returns the BroadcastHandler from the pipeline. If not, an upstream
   * connection is created and stored along with a new broadcast pipeline
   * for this routingData, and its BroadcastHandler is returned.
   *
   * Caller should immediately subscribe to the returned BroadcastHandler
   * to prevent it from being garbage collected.
   */
  virtual folly::Future<BroadcastHandler<T, R>*> getHandler(
      const R& routingData);

  /**
   * Checks if a broadcast is available locally for the given routingData.
   */
  bool isBroadcasting(const R& routingData) {
    return (broadcasts_.find(routingData) != broadcasts_.end());
  }

 private:
  void deleteBroadcast(const R& routingData) { broadcasts_.erase(routingData); }

  std::shared_ptr<ServerPool<R>> serverPool_;
  std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory_;
  std::map<R, std::unique_ptr<BroadcastManager>> broadcasts_;
};

} // namespace wangle

#include <wangle/channel/broadcast/BroadcastPool-inl.h>

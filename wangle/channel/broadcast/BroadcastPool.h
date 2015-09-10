// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

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

    folly::Future<BroadcastHandler<T>*> getHandler();

    // PipelineManager implementation
    void deletePipeline(PipelineBase* pipeline) override {
      CHECK(client_.getPipeline() == pipeline);
      broadcastPool_->deleteBroadcast(routingData_);
    }

   private:
    BroadcastPool<T, R>* broadcastPool_{nullptr};
    R routingData_;
    ClientBootstrap<DefaultPipeline> client_;

    bool connectStarted_{false};
    folly::SharedPromise<BroadcastHandler<T>*> sharedPromise_;
  };

  BroadcastPool(std::shared_ptr<ServerPool<R>> serverPool,
                std::shared_ptr<BroadcastPipelineFactory<T, R>> pipelineFactory)
      : serverPool_(serverPool), broadcastPipelineFactory_(pipelineFactory) {}

  virtual ~BroadcastPool() {}

  /**
   * Gets the BroadcastHandler, or creates one if it doesn't exist already,
   * for the given routingData.
   *
   * If a broadcast is already available for the given routingData,
   * returns the BroadcastHandler from the pipeline. If not, an upstream
   * connection is created and stored along with a new broadcast pipeline
   * for this routingData, and its BroadcastHandler is returned.
   */
  virtual folly::Future<BroadcastHandler<T>*> getHandler(const R& routingData);

  /**
   * Checks if a broadcast is available locally for the given routingData.
   */
  bool isBroadcasting(const R& routingData) {
    return (broadcasts_.find(routingData) != broadcasts_.end());
  }

 private:
  void deleteBroadcast(const R& routingData) {
    broadcasts_.erase(routingData);
  }

  std::shared_ptr<ServerPool<R>> serverPool_;
  std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory_;
  std::map<R, std::unique_ptr<BroadcastManager>> broadcasts_;
};

} // namespace wangle

#include <wangle/channel/broadcast/BroadcastPool-inl.h>

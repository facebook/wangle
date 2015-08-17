// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <wangle/bootstrap/RoutingDataHandler.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/Pipeline.h>

namespace wangle {

/**
 * An AcceptPipeline with the ability to hash connections to
 * a specific worker thread. Hashing can be based on data passed
 * in by the client.
 *
 * For each connection, AcceptRoutingHandler creates and maintains a routing
 * pipeline internally. The routing pipeline should take care of
 * reading from the socket, parsing the data based on which to hash
 * the connection and invoking RoutingDataHandler::Callback::onRoutingData
 * to notify the AcceptRoutingHandler. AcceptRoutingHandler then pauses
 * reads from the socket, moves the connection over to the hashed
 * worker thread, and resumes reading from the socket on the child pipeline.
 */

typedef PipelineFactory<AcceptPipeline> AcceptPipelineFactory;

template <typename Pipeline, typename R>
class RoutingDataPipelineFactory;

template <typename Pipeline, typename R>
class AcceptRoutingHandler : public wangle::InboundHandler<void*>,
                             public RoutingDataHandler<R>::Callback {
 public:
  AcceptRoutingHandler(
      ServerBootstrap<Pipeline>* server,
      std::shared_ptr<RoutingDataHandlerFactory<R>> routingHandlerFactory,
      std::shared_ptr<RoutingDataPipelineFactory<Pipeline, R>>
          childPipelineFactory)
      : server_(CHECK_NOTNULL(server)),
        routingHandlerFactory_(routingHandlerFactory),
        childPipelineFactory_(childPipelineFactory) {}

  // InboundHandler implementation
  void read(Context* ctx, void* conn) override;

  // RoutingDataHandler::Callback implementation
  void onRoutingData(
      uint64_t connId,
      typename RoutingDataHandler<R>::RoutingData& routingData) override;
  void onError(uint64_t connId) override;

 private:
  void populateAcceptors();

  ServerBootstrap<Pipeline>* server_;
  std::shared_ptr<RoutingDataHandlerFactory<R>> routingHandlerFactory_;
  std::shared_ptr<RoutingDataPipelineFactory<Pipeline, R>>
      childPipelineFactory_;

  std::vector<Acceptor*> acceptors_;
  std::map<uint64_t, DefaultPipeline::UniquePtr> routingPipelines_;
  uint64_t nextConnId_{0};
};

template <typename Pipeline, typename R>
class AcceptRoutingPipelineFactory : public AcceptPipelineFactory {
 public:
  AcceptRoutingPipelineFactory(
      ServerBootstrap<Pipeline>* server,
      std::shared_ptr<RoutingDataHandlerFactory<R>> routingHandlerFactory,
      std::shared_ptr<RoutingDataPipelineFactory<Pipeline, R>>
          childPipelineFactory)
      : server_(CHECK_NOTNULL(server)),
        routingHandlerFactory_(routingHandlerFactory),
        childPipelineFactory_(childPipelineFactory) {}

  AcceptPipeline::UniquePtr newPipeline(
      std::shared_ptr<folly::AsyncSocket>) override {
    AcceptPipeline::UniquePtr pipeline(new AcceptPipeline);
    pipeline->addBack(AcceptRoutingHandler<Pipeline, R>(
        server_, routingHandlerFactory_, childPipelineFactory_));
    pipeline->finalize();

    return pipeline;
  }

 private:
  ServerBootstrap<Pipeline>* server_;
  std::shared_ptr<RoutingDataHandlerFactory<R>> routingHandlerFactory_;
  std::shared_ptr<RoutingDataPipelineFactory<Pipeline, R>>
      childPipelineFactory_;
};

template <typename Pipeline, typename R>
class RoutingDataPipelineFactory {
 public:
  virtual ~RoutingDataPipelineFactory() {}

  virtual typename Pipeline::UniquePtr newPipeline(
      std::shared_ptr<folly::AsyncSocket> socket, const R& routingData) = 0;
};

} // namespace wangle

#include <wangle/bootstrap/AcceptRoutingHandler-inl.h>

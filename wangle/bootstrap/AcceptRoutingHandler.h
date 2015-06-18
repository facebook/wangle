// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <wangle/bootstrap/RoutingDataHandler.h>
#include <wangle/bootstrap/ServerBootstrap.h>

namespace folly { namespace wangle {

typedef folly::PipelineFactory<folly::AcceptPipeline> AcceptPipelineFactory;

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

template <typename Pipeline>
class AcceptRoutingHandler : public folly::wangle::InboundHandler<void*>,
                             public RoutingDataHandler::Callback {
 public:
  AcceptRoutingHandler(
      folly::ServerBootstrap<Pipeline>* server,
      std::shared_ptr<RoutingDataHandlerFactory> routingHandlerFactory,
      std::shared_ptr<folly::PipelineFactory<Pipeline>> childPipelineFactory)
      : server_(CHECK_NOTNULL(server)),
        routingHandlerFactory_(routingHandlerFactory),
        childPipelineFactory_(childPipelineFactory) {}

  // InboundHandler implementation
  void read(Context* ctx, void* conn) override;

  // RoutingDataHandler::Callback implementation
  void onRoutingData(uint64_t connId,
                     RoutingDataHandler::RoutingData& routingData) override;
  void onError(uint64_t connId) override;

 private:
  void populateAcceptors();

  folly::ServerBootstrap<Pipeline>* server_;
  std::shared_ptr<RoutingDataHandlerFactory> routingHandlerFactory_;
  std::shared_ptr<folly::PipelineFactory<Pipeline>> childPipelineFactory_;

  std::vector<folly::Acceptor*> acceptors_;
  std::map<uint64_t, folly::DefaultPipeline::UniquePtr> routingPipelines_;
  uint64_t nextConnId_{0};
};

template <typename Pipeline>
class AcceptRoutingPipelineFactory : public AcceptPipelineFactory {
 public:
  AcceptRoutingPipelineFactory(
      folly::ServerBootstrap<Pipeline>* server,
      std::shared_ptr<RoutingDataHandlerFactory> routingHandlerFactory,
      std::shared_ptr<folly::PipelineFactory<Pipeline>> childPipelineFactory)
      : server_(CHECK_NOTNULL(server)),
        routingHandlerFactory_(routingHandlerFactory),
        childPipelineFactory_(childPipelineFactory) {}

  folly::AcceptPipeline::UniquePtr newPipeline(
      std::shared_ptr<folly::AsyncSocket>) override {
    folly::AcceptPipeline::UniquePtr pipeline(new folly::AcceptPipeline);
    pipeline->addBack(AcceptRoutingHandler<Pipeline>(
        server_, routingHandlerFactory_, childPipelineFactory_));
    pipeline->finalize();

    return pipeline;
  }

 private:
  folly::ServerBootstrap<Pipeline>* server_;
  std::shared_ptr<RoutingDataHandlerFactory> routingHandlerFactory_;
  std::shared_ptr<folly::PipelineFactory<Pipeline>> childPipelineFactory_;
};

}} // namespace folly::wangle

#include <wangle/bootstrap/AcceptRoutingHandler-inl.h>

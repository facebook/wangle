/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <wangle/bootstrap/AcceptRoutingHandler.h>
#include <wangle/bootstrap/ClientBootstrap.h>
#include <wangle/bootstrap/RoutingDataHandler.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/test/MockHandler.h>

#include <boost/thread.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace wangle {

/**
 * An accept pipeline factory that returns a known accept pipeline with
 * known accept routing handler, and routing pipeline.
 */
class MockAcceptPipelineFactory : public AcceptPipelineFactory {
 public:
  explicit MockAcceptPipelineFactory(AcceptPipeline::Ptr pipeline)
      : pipeline_(pipeline) {}

  AcceptPipeline::Ptr newPipeline(Acceptor*) override {
    return pipeline_;
  }

 protected:
  AcceptPipeline::Ptr pipeline_;
};

/**
 * An AcceptRoutingHandler that handles a specific routingPipeline.
 */
class MockAcceptRoutingHandler
    : public AcceptRoutingHandler<DefaultPipeline, char> {
 public:
  MockAcceptRoutingHandler(
      ServerBootstrap<DefaultPipeline>* server,
      std::shared_ptr<RoutingDataHandlerFactory<char>> routingHandlerFactory,
      std::shared_ptr<RoutingDataPipelineFactory<DefaultPipeline, char>>
          childPipelineFactory,
      DefaultPipeline::Ptr routingPipeline)
      : AcceptRoutingHandler(
            server,
            routingHandlerFactory,
            childPipelineFactory),
        routingPipeline_(routingPipeline) {}

 protected:
  DefaultPipeline::Ptr newRoutingPipeline() override {
    return routingPipeline_;
  }
  DefaultPipeline::Ptr routingPipeline_;
};

class MockRoutingDataHandler : public RoutingDataHandler<char> {
 public:
  MockRoutingDataHandler(uint64_t connId, Callback* cob)
      : RoutingDataHandler<char>(connId, cob) {}
  MOCK_METHOD1(transportActive, void(Context*));
  MOCK_METHOD2(
      parseRoutingData,
      bool(folly::IOBufQueue& bufQueue, RoutingData& routingData));
  MOCK_METHOD2(readException, void(Context*, folly::exception_wrapper ex));
};

class MockRoutingDataHandlerFactory : public RoutingDataHandlerFactory<char> {
 public:
  MockRoutingDataHandlerFactory() {}

  std::shared_ptr<RoutingDataHandler<char>> newHandler(
      uint64_t /*connId*/,
      RoutingDataHandler<char>::Callback* /*cob*/) override {
    VLOG(4) << "New pipeline with a test routing handler";
    return std::shared_ptr<RoutingDataHandler<char>>(routingDataHandler_);
  }
  void setRoutingDataHandler(MockRoutingDataHandler* routingDataHandler) {
    routingDataHandler_ = routingDataHandler;
  }

 protected:
  MockRoutingDataHandler* routingDataHandler_;
};

class MockDownstreamPipelineFactory
    : public RoutingDataPipelineFactory<DefaultPipeline, char> {
 public:
  explicit MockDownstreamPipelineFactory(
    MockBytesToBytesHandler* downstreamHandler)
      : downstreamHandler_(downstreamHandler) {}
  DefaultPipeline::Ptr newPipeline(
      std::shared_ptr<folly::AsyncSocket> socket,
      const char& /*routingData*/,
      RoutingDataHandler<char>* /*handler*/,
      std::shared_ptr<TransportInfo> /*transportInfo*/) override {
    auto pipeline = DefaultPipeline::create();
    pipeline->addBack(AsyncSocketHandler(socket));
    pipeline->addBack(
        std::shared_ptr<MockBytesToBytesHandler>(downstreamHandler_));
    pipeline->finalize();
    return pipeline;
  }

 protected:
  MockBytesToBytesHandler* downstreamHandler_;
};
}

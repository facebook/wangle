/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "wangle/bootstrap/ServerBootstrap.h"
#include "wangle/bootstrap/ClientBootstrap.h"
#include "wangle/channel/Handler.h"
#include "wangle/channel/broadcast/ObservingHandler.h"

#include <glog/logging.h>
#include <folly/portability/GTest.h>

using namespace wangle;
using namespace folly;
using namespace testing;

using BytesPipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>;
using TestObsPipeline = ObservingPipeline<std::shared_ptr<folly::IOBuf>>;

using TestServer = ServerBootstrap<BytesPipeline>;
using TestClient = ClientBootstrap<TestObsPipeline>;

struct TestRoutingData {
  std::string data;
  bool operator==(const TestRoutingData& other) const {
    return this->data == other.data;
  }
  bool operator<(const TestRoutingData& other) const {
    return this->data < other.data;
  }
};

class TestPipelineFactory : public PipelineFactory<BytesPipeline> {
 public:
  BytesPipeline::Ptr newPipeline(
      std::shared_ptr<AsyncTransportWrapper> /* unused */) override {
    pipelines_++;
    auto pipeline = BytesPipeline::create();
    pipeline->addBack(new BytesToBytesHandler());
    pipeline->finalize();
    return pipeline;
  }
  std::atomic<int> pipelines_{0};
};

class CustomPipelineFactory
    : public TestPipelineFactory,
      public ObservingPipelineFactory<
               std::shared_ptr<folly::IOBuf>, TestRoutingData> {
 public:
  CustomPipelineFactory()
  : ObservingPipelineFactory<std::shared_ptr<folly::IOBuf>, TestRoutingData>(
      nullptr, nullptr) {
  }

  TestObsPipeline::Ptr newPipeline(
      std::shared_ptr<folly::AsyncSocket> socket,
      const TestRoutingData& routingData,
      RoutingDataHandler<TestRoutingData>* /* unused */,
      std::shared_ptr<TransportInfo> /* unused */) override {
    routingData_ = routingData;
    auto pipeline = TestObsPipeline::create();
    pipeline->addBack(AsyncSocketHandler(socket));
    pipeline->finalize();
    routingPipelines_++;
    return pipeline;
  }

  BytesPipeline::Ptr newPipeline(
      std::shared_ptr<AsyncTransportWrapper> sock) override {
    // Should not be called.
    ADD_FAILURE() << "Should not be called, "
                  << "this function is typically called from "
                  << "makePipeline that has been overridden in this "
                  << "test to call a different version of newPipeline.";
    return TestPipelineFactory::newPipeline(sock);
  }

  TestRoutingData routingData_;
  std::atomic<int> routingPipelines_{0};
};

class CustomPipelineMakerTestClient : public TestClient {
 public:
  explicit CustomPipelineMakerTestClient(
      const TestRoutingData& routingData,
      const std::shared_ptr<CustomPipelineFactory>& factory)
      : routingData_(routingData),
        factory_(factory) {
  }

  void makePipeline(std::shared_ptr<folly::AsyncSocket> socket) override {
    setPipeline(factory_->newPipeline(
      socket, routingData_, nullptr, nullptr));
  }

  TestRoutingData routingData_;
  std::shared_ptr<CustomPipelineFactory> factory_;
};

TEST(ObservingClientPipelineTest, CustomPipelineMaker) {
  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);
  server.bind(0);
  auto base = EventBaseManager::get()->getEventBase();

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  TestRoutingData routingData;
  routingData.data = "Test";
  auto clientPipelineFactory = std::make_shared<CustomPipelineFactory>();
  auto client =
    std::make_unique<CustomPipelineMakerTestClient>(
        routingData, clientPipelineFactory);

  client->connect(address, std::chrono::milliseconds(0));
  base->loop();
  server.stop();
  server.join();

  EXPECT_EQ(1, clientPipelineFactory->routingPipelines_);
  EXPECT_EQ(routingData, clientPipelineFactory->routingData_);
  EXPECT_EQ(0, clientPipelineFactory->pipelines_);
}

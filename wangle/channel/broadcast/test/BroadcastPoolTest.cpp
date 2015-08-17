// Copyright 2004-present Facebook.  All rights reserved.
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/broadcast/BroadcastPool.h>
#include <wangle/channel/broadcast/test/Mocks.h>

using namespace wangle;
using namespace folly;
using namespace testing;

class BroadcastPoolTest : public Test {
 public:
  void SetUp() override {
    startServer();

    serverPool = std::make_shared<StrictMock<MockServerPool>>();
    EXPECT_CALL(*serverPool, getServer()).WillRepeatedly(ReturnPointee(&addr));

    pipelineFactory =
        std::make_shared<StrictMock<MockBroadcastPipelineFactory>>();
    pool = folly::make_unique<BroadcastPool<int, std::string>>(serverPool,
                                                               pipelineFactory);
  }

  void TearDown() override {
    Mock::VerifyAndClear(serverPool.get());
    Mock::VerifyAndClear(pipelineFactory.get());

    serverPool.reset();
    pipelineFactory.reset();
    pool.reset();

    stopServer();
  }

 protected:
  class ServerPipelineFactory : public PipelineFactory<DefaultPipeline> {
   public:
    DefaultPipeline::UniquePtr newPipeline(
        std::shared_ptr<AsyncSocket> sock) override {
      return DefaultPipeline::UniquePtr(new DefaultPipeline);
    }
  };

  void startServer() {
    server = folly::make_unique<ServerBootstrap<DefaultPipeline>>();
    server->childPipeline(std::make_shared<ServerPipelineFactory>());
    server->bind(0);
    server->getSockets()[0]->getAddress(&addr);
  }

  void stopServer() {
    server.reset();
  }

  std::unique_ptr<BroadcastPool<int, std::string>> pool;
  std::shared_ptr<StrictMock<MockServerPool>> serverPool;
  std::shared_ptr<StrictMock<MockBroadcastPipelineFactory>> pipelineFactory;
  std::unique_ptr<ServerBootstrap<DefaultPipeline>> server;
  SocketAddress addr;
};

TEST_F(BroadcastPoolTest, BasicConnect) {
  // Test simple calls to getHandler()
  std::string routingData1 = "url1";
  std::string routingData2 = "url2";
  BroadcastHandler<int>* handler1 = nullptr;
  BroadcastHandler<int>* handler2 = nullptr;
  auto base = EventBaseManager::get()->getEventBase();

  InSequence dummy;

  // No broadcast available for routingData1. Test that a new connection
  // is established and handler created.
  EXPECT_FALSE(pool->isBroadcasting(routingData1));
  pool->getHandler(routingData1)
      .then([&](BroadcastHandler<int>* h) {
        handler1 = h;
      });
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_CALL(*pipelineFactory, setRoutingData(_, "url1")).Times(1);
  base->loopOnce(); // Do async connect
  EXPECT_TRUE(handler1 != nullptr);
  EXPECT_TRUE(pool->isBroadcasting(routingData1));

  // Broadcast available for routingData1. Test that the same handler
  // is returned.
  pool->getHandler(routingData1)
      .then([&](BroadcastHandler<int>* h) {
        EXPECT_TRUE(h == handler1);
      })
      .wait();
  EXPECT_TRUE(pool->isBroadcasting(routingData1));

  // Close the handler. This will delete the pipeline and the broadcast.
  handler1->close(handler1->getContext());
  EXPECT_FALSE(pool->isBroadcasting(routingData1));

  // routingData1 doesn't have an available broadcast now. Test that a
  // new connection is established again and handler created.
  handler1 = nullptr;
  pool->getHandler(routingData1)
      .then([&](BroadcastHandler<int>* h) {
        handler1 = h;
      });
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_CALL(*pipelineFactory, setRoutingData(_, "url1")).Times(1);
  base->loopOnce(); // Do async connect
  EXPECT_TRUE(handler1 != nullptr);
  EXPECT_TRUE(pool->isBroadcasting(routingData1));

  // Test that a new connection is established for routingData2 with
  // a new handler created
  EXPECT_FALSE(pool->isBroadcasting(routingData2));
  pool->getHandler(routingData2)
      .then([&](BroadcastHandler<int>* h) {
        handler2 = h;
      });
  EXPECT_TRUE(handler2 == nullptr);
  EXPECT_CALL(*pipelineFactory, setRoutingData(_, "url2")).Times(1);
  base->loopOnce(); // Do async connect
  EXPECT_TRUE(handler2 != nullptr);
  EXPECT_TRUE(handler2 != handler1);
  EXPECT_TRUE(pool->isBroadcasting(routingData2));
}

TEST_F(BroadcastPoolTest, OutstandingConnect) {
  // Test with multiple getHandler() calls for the same routing data
  // when a connect request is in flight
  std::string routingData = "url1";
  BroadcastHandler<int>* handler1 = nullptr;
  BroadcastHandler<int>* handler2 = nullptr;
  auto base = EventBaseManager::get()->getEventBase();

  InSequence dummy;

  // No broadcast available for routingData. Kick off a connect request.
  EXPECT_FALSE(pool->isBroadcasting(routingData));
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        handler1 = h;
      });
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_TRUE(pool->isBroadcasting(routingData));

  // Invoke getHandler() for the same routing data when a connect request
  // is outstanding
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        handler2 = h;
      });
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_TRUE(handler2 == nullptr);
  EXPECT_TRUE(pool->isBroadcasting(routingData));

  EXPECT_CALL(*pipelineFactory, setRoutingData(_, "url1")).Times(1);

  base->loopOnce(); // Do async connect

  // Verify that both promises are fulfilled
  EXPECT_TRUE(handler1 != nullptr);
  EXPECT_TRUE(handler2 != nullptr);
  EXPECT_TRUE(handler1 == handler2);
  EXPECT_TRUE(pool->isBroadcasting(routingData));

  // Invoke getHandler() again to test if the same handler is returned
  // from the existing connection
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        EXPECT_TRUE(h == handler1);
      })
      .wait();
  EXPECT_TRUE(pool->isBroadcasting(routingData));
}

TEST_F(BroadcastPoolTest, ConnectError) {
  // Test when an exception occurs during connect request
  std::string routingData = "url1";
  BroadcastHandler<int>* handler1 = nullptr;
  BroadcastHandler<int>* handler2 = nullptr;
  bool handler1Error = false;
  bool handler2Error = false;
  auto base = EventBaseManager::get()->getEventBase();

  InSequence dummy;

  // Stop the server to inject connect failure
  stopServer();

  // No broadcast available for routingData. Kick off a connect request.
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        handler1 = h;
      })
      .onError([&] (const std::exception& ex) {
        handler1Error = true;
        EXPECT_FALSE(pool->isBroadcasting(routingData));
      });
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_FALSE(handler1Error);
  EXPECT_TRUE(pool->isBroadcasting(routingData));

  // Invoke getHandler() again while the connect request is in flight
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        handler2 = h;
      })
      .onError([&] (const std::exception& ex) {
        handler2Error = true;
        EXPECT_FALSE(pool->isBroadcasting(routingData));
      });
  EXPECT_TRUE(handler2 == nullptr);
  EXPECT_FALSE(handler2Error);
  EXPECT_TRUE(pool->isBroadcasting(routingData));

  base->loopOnce(); // Do async connect

  // Verify that the exception is set on both promises
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_TRUE(handler2 == nullptr);
  EXPECT_TRUE(handler1Error);
  EXPECT_TRUE(handler2Error);

  // The broadcast should have been deleted now
  EXPECT_FALSE(pool->isBroadcasting(routingData));

  // Start the server now. Connect requests should succeed.
  startServer();
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        handler1 = h;
      });
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_CALL(*pipelineFactory, setRoutingData(_, "url1")).Times(1);
  base->loopOnce(); // Do async connect
  EXPECT_TRUE(handler1 != nullptr);
  EXPECT_TRUE(pool->isBroadcasting(routingData));
}

TEST_F(BroadcastPoolTest, ConnectErrorPoolDeletion) {
  // Test against use-after-free when connect error deletes the pool
  std::string routingData = "url1";
  auto base = EventBaseManager::get()->getEventBase();

  InSequence dummy;

  // Stop the server to inject connect failure
  stopServer();

  pool->getHandler(routingData)
      .then()
      .onError([&](const std::exception& ex) {
        // The broadcast should have been deleted by now. Delete the pool.
        EXPECT_FALSE(pool->isBroadcasting(routingData));
        pool.reset();
      });
  EXPECT_TRUE(pool->isBroadcasting(routingData));
  base->loopOnce();
  EXPECT_TRUE(pool.get() == nullptr);
}

TEST_F(BroadcastPoolTest, HandlerEOFPoolDeletion) {
  // Test against use-after-free on BroadcastManager when the pool
  // is deleted before the handler
  std::string routingData = "url1";
  BroadcastHandler<int>* handler = nullptr;
  DefaultPipeline* pipeline = nullptr;
  StrictMock<MockSubscriber<int>> subscriber;
  auto base = EventBaseManager::get()->getEventBase();

  InSequence dummy;

  // Dispatch a connect request and create a handler
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        handler = h;
        pipeline = dynamic_cast<DefaultPipeline*>(
            handler->getContext()->getPipeline());
      });
  EXPECT_CALL(*pipelineFactory, setRoutingData(_, "url1")).Times(1);
  base->loopOnce(); // Do async connect
  EXPECT_TRUE(pool->isBroadcasting(routingData));
  EXPECT_TRUE(handler != nullptr);
  EXPECT_TRUE(pipeline != nullptr);

  handler->subscribe(&subscriber);

  EXPECT_CALL(subscriber, onCompleted())
      .WillOnce(InvokeWithoutArgs([&] {
        // Forcefully delete the pool
        pool.reset();
      }));

  // This will also delete the pipeline and the handler
  pipeline->readEOF();
}

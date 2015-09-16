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
    addr = std::make_shared<SocketAddress>();
    serverPool = std::make_shared<StrictMock<MockServerPool>>(addr);

    pipelineFactory =
        std::make_shared<StrictMock<MockBroadcastPipelineFactory>>();
    pool = BroadcastPool<int, std::string>::get(serverPool, pipelineFactory);

    startServer();
  }

  void TearDown() override {
    Mock::VerifyAndClear(serverPool.get());
    Mock::VerifyAndClear(pipelineFactory.get());

    serverPool.reset();
    addr.reset();
    pipelineFactory.reset();
    pool = nullptr;

    stopServer();
  }

 protected:
  class ServerPipelineFactory : public PipelineFactory<DefaultPipeline> {
   public:
    DefaultPipeline::Ptr newPipeline(
        std::shared_ptr<AsyncSocket> sock) override {
      return DefaultPipeline::create();
    }
  };

  void startServer() {
    server = folly::make_unique<ServerBootstrap<DefaultPipeline>>();
    server->childPipeline(std::make_shared<ServerPipelineFactory>());
    server->bind(0);
    server->getSockets()[0]->getAddress(addr.get());
  }

  void stopServer() {
    server.reset();
  }

  BroadcastPool<int, std::string>* pool{nullptr};
  std::shared_ptr<StrictMock<MockServerPool>> serverPool;
  std::shared_ptr<StrictMock<MockBroadcastPipelineFactory>> pipelineFactory;
  std::unique_ptr<ServerBootstrap<DefaultPipeline>> server;
  std::shared_ptr<SocketAddress> addr;
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

TEST_F(BroadcastPoolTest, ConnectErrorServerPool) {
  // Test when an error occurs in ServerPool when trying to kick off
  // a connect request
  std::string routingData = "url1";
  BroadcastHandler<int>* handler1 = nullptr;
  BroadcastHandler<int>* handler2 = nullptr;
  bool handler1Error = false;
  bool handler2Error = false;
  auto base = EventBaseManager::get()->getEventBase();

  InSequence dummy;

  // Inject a ServerPool error
  serverPool->failConnect();
  pool->getHandler(routingData)
      .then([&](BroadcastHandler<int>* h) {
        handler1 = h;
      })
      .onError([&] (const std::exception& ex) {
        handler1Error = true;
        EXPECT_FALSE(pool->isBroadcasting(routingData));
      });
  EXPECT_TRUE(handler1 == nullptr);
  EXPECT_TRUE(handler1Error);
  EXPECT_FALSE(pool->isBroadcasting(routingData));
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

  EXPECT_CALL(subscriber, onCompleted()).Times(1);

  // This will also delete the pipeline and the handler
  pipeline->readEOF();
  EXPECT_FALSE(pool->isBroadcasting(routingData));
}

TEST_F(BroadcastPoolTest, ThreadLocalPool) {
  // Test that thread-local static broadcast pool works correctly
  BroadcastHandler<int>* broadcastHandler = nullptr;

  InSequence dummy;

  // There should be no broadcast available for this routing data
  EXPECT_FALSE(
      (BroadcastPool<int, std::string>::get(serverPool, pipelineFactory)
           ->isBroadcasting("/url1")));

  // Test creating a new broadcast
  EXPECT_CALL(*pipelineFactory, setRoutingData(_, "/url1"))
      .WillOnce(Invoke([&](DefaultPipeline* pipeline, const std::string&) {
        broadcastHandler = pipelineFactory->getBroadcastHandler(pipeline);
      }));
  auto handler = std::make_shared<ObservingHandler<int, std::string>>(
      "/url1", serverPool, pipelineFactory);
  auto pipeline1 = Pipeline<IOBufQueue&, int>::create();
  pipeline1->addBack(std::move(handler));
  pipeline1->finalize();
  pipeline1->transportActive();
  EventBaseManager::get()->getEventBase()->loopOnce();
  EXPECT_TRUE((BroadcastPool<int, std::string>::get(serverPool, pipelineFactory)
                   ->isBroadcasting("/url1")));

  // Test broadcast with the same routing data in the same thread. No
  // new broadcast handler should be created.
  EXPECT_CALL(*pipelineFactory, setRoutingData(_, _)).Times(0);
  handler = std::make_shared<ObservingHandler<int, std::string>>(
      "/url1", serverPool, pipelineFactory);
  auto pipeline2 = Pipeline<IOBufQueue&, int>::create();
  pipeline2->addBack(std::move(handler));
  pipeline2->finalize();
  pipeline2->transportActive();
  EXPECT_TRUE((BroadcastPool<int, std::string>::get(serverPool, pipelineFactory)
                   ->isBroadcasting("/url1")));

  // Test creating a broadcast with the same routing data but in a
  // different thread. Should return a different broadcast handler.
  std::thread([&] {
    // There should be no broadcast available for this routing data since we
    // are on a different thread.
    EXPECT_FALSE(
        (BroadcastPool<int, std::string>::get(serverPool, pipelineFactory)
             ->isBroadcasting("/url1")));

    EXPECT_CALL(*pipelineFactory, setRoutingData(_, "/url1"))
        .WillOnce(Invoke([&](DefaultPipeline* pipeline, const std::string&) {
          EXPECT_NE(pipelineFactory->getBroadcastHandler(pipeline),
                    broadcastHandler);
        }));
    auto handler = std::make_shared<ObservingHandler<int, std::string>>(
        "/url1", serverPool, pipelineFactory);
    auto pipeline3 = Pipeline<IOBufQueue&, int>::create();
    pipeline3->addBack(std::move(handler));
    pipeline3->finalize();
    pipeline3->transportActive();
    EventBaseManager::get()->getEventBase()->loopOnce();
    EXPECT_TRUE(
        (BroadcastPool<int, std::string>::get(serverPool, pipelineFactory)
             ->isBroadcasting("/url1")));
    pipeline3->readEOF();
  }).join();

  // Cleanup
  pipeline1->readEOF();
  pipeline2->readEOF();
}

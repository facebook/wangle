// Copyright 2004-present Facebook.  All rights reserved.
#include <wangle/channel/broadcast/test/Mocks.h>

using namespace folly::wangle;
using namespace folly;
using namespace testing;

class ObservingHandlerTest : public Test {
 public:
  class MockBroadcastHandler : public BroadcastHandler<std::unique_ptr<IOBuf>> {
   public:
    MOCK_METHOD2(processRead,
                 bool(folly::IOBufQueue&, std::unique_ptr<IOBuf>&));
    MOCK_METHOD1(subscribe, uint64_t(Subscriber<std::unique_ptr<IOBuf>>*));
    MOCK_METHOD1(unsubscribe, void(uint64_t));
  };

  void SetUp() override {
    socketHandler = make_unique<StrictMock<MockAsyncSocketHandler>>();
    observingHandler = make_unique<StrictMock<MockObservingHandler>>();
    broadcastHandler = make_unique<StrictMock<MockBroadcastHandler>>();

    pipeline.reset(new DefaultPipeline);
    pipeline->addBack(socketHandler.get());
    pipeline->addBack(observingHandler.get());
    pipeline->finalize();

    pool = new MockBroadcastPool<std::unique_ptr<IOBuf>>();
  }

  void TearDown() override {
    Mock::VerifyAndClear(socketHandler.get());
    Mock::VerifyAndClear(observingHandler.get());
    Mock::VerifyAndClear(broadcastHandler.get());

    pipeline.reset();

    socketHandler.reset();
    observingHandler.reset();
    broadcastHandler.reset();
  }

 protected:
  DefaultPipeline::UniquePtr pipeline;

  std::unique_ptr<StrictMock<MockAsyncSocketHandler>> socketHandler;
  std::unique_ptr<StrictMock<MockObservingHandler>> observingHandler;
  std::unique_ptr<StrictMock<MockBroadcastHandler>> broadcastHandler;

  MockBroadcastPool<std::unique_ptr<IOBuf>>* pool{nullptr};
};

TEST_F(ObservingHandlerTest, Success) {
  InSequence dummy;

  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*socketHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(*observingHandler, newBroadcastPool()).WillOnce(Return(pool));
  EXPECT_CALL(*pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<std::unique_ptr<IOBuf>>*>(
            std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  // Test broadcasting empty buf
  std::unique_ptr<IOBuf> buf;
  observingHandler->onNext(buf);

  EXPECT_CALL(*observingHandler, write(_, _)).Times(2);

  // Broadcast some data
  buf = IOBuf::copyBuffer("data");
  observingHandler->onNext(buf);
  observingHandler->onNext(buf);

  EXPECT_CALL(*observingHandler, write(_, _)).Times(0);

  // Broadcast some data while the handler is paused
  observingHandler->pause();
  observingHandler->onNext(buf);

  EXPECT_CALL(*observingHandler, write(_, _)).Times(1);

  // Resume the handler and broadcast more data
  observingHandler->resume();
  observingHandler->onNext(buf);

  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Finish the broadcast
  observingHandler->onCompleted();
}

TEST_F(ObservingHandlerTest, BroadcastError) {
  InSequence dummy;

  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*socketHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(*observingHandler, newBroadcastPool()).WillOnce(Return(pool));
  EXPECT_CALL(*pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<std::unique_ptr<IOBuf>>*>(
            std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _)).Times(1);

  // Broadcast some data
  auto buf = IOBuf::copyBuffer("data");
  observingHandler->onNext( buf);

  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Inject broadcast error
  observingHandler->onError(make_exception_wrapper<std::exception>());
}

TEST_F(ObservingHandlerTest, ReadEOF) {
  InSequence dummy;

  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*socketHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(*observingHandler, newBroadcastPool()).WillOnce(Return(pool));
  EXPECT_CALL(*pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<std::unique_ptr<IOBuf>>*>(
            std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _)).Times(1);

  // Broadcast some data
  auto buf = IOBuf::copyBuffer("data");
  observingHandler->onNext( buf);

  EXPECT_CALL(*broadcastHandler, unsubscribe(_)).Times(1);
  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Client closes connection
  pipeline->readEOF();
}

TEST_F(ObservingHandlerTest, ReadError) {
  InSequence dummy;

  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*socketHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(*observingHandler, newBroadcastPool()).WillOnce(Return(pool));
  EXPECT_CALL(*pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<std::unique_ptr<IOBuf>>*>(
            std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _)).Times(1);

  // Broadcast some data
  auto buf = IOBuf::copyBuffer("data");
  observingHandler->onNext( buf);

  EXPECT_CALL(*broadcastHandler, unsubscribe(_)).Times(1);
  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Inject read error
  pipeline->readException(make_exception_wrapper<std::exception>());
}

TEST_F(ObservingHandlerTest, WriteError) {
  InSequence dummy;

  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*socketHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(*observingHandler, newBroadcastPool()).WillOnce(Return(pool));
  EXPECT_CALL(*pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<std::unique_ptr<IOBuf>>*>(
            std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*socketHandler, transportActive(_))
      .WillOnce(Invoke([&](MockAsyncSocketHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _))
      .WillOnce(InvokeWithoutArgs([&] {
        // Inject write error
        return makeFuture<Unit>(make_exception_wrapper<std::exception>());
      }));
  EXPECT_CALL(*broadcastHandler, unsubscribe(_)).Times(1);
  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Broadcast some data
  auto buf = IOBuf::copyBuffer("data");
  observingHandler->onNext( buf);
}

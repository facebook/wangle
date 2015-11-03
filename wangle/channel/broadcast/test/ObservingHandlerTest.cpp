/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/channel/broadcast/test/Mocks.h>
#include <wangle/channel/test/MockHandler.h>
#include <wangle/codec/MessageToByteEncoder.h>

using namespace wangle;
using namespace folly;
using namespace testing;

class ObservingHandlerTest : public Test {
 public:
  class MockIntToByteEncoder : public MessageToByteEncoder<int> {
   public:
    std::unique_ptr<IOBuf> encode(int& data) override {
      return IOBuf::copyBuffer(folly::to<std::string>(data));
    }
  };

  void SetUp() override {
    prevHandler = new StrictMock<MockBytesToBytesHandler>();
    observingHandler = new StrictMock<MockObservingHandler>(&pool);
    broadcastHandler = make_unique<StrictMock<MockBroadcastHandler>>();

    pipeline = ObservingPipeline<int>::create();
    pipeline->addBack(
        std::shared_ptr<StrictMock<MockBytesToBytesHandler>>(prevHandler));
    pipeline->addBack(MockIntToByteEncoder());
    pipeline->addBack(
        std::shared_ptr<StrictMock<MockObservingHandler>>(observingHandler));
    pipeline->finalize();
  }

  void TearDown() override {
    Mock::VerifyAndClear(broadcastHandler.get());

    broadcastHandler.reset();
    pipeline.reset();
  }

 protected:
  ObservingPipeline<int>::Ptr pipeline;

  StrictMock<MockBytesToBytesHandler>* prevHandler{nullptr};
  StrictMock<MockObservingHandler>* observingHandler{nullptr};
  std::unique_ptr<StrictMock<MockBroadcastHandler>> broadcastHandler;

  StrictMock<MockBroadcastPool> pool;
};

TEST_F(ObservingHandlerTest, Success) {
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<int>*>(std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _)).Times(2);

  // Broadcast some data
  observingHandler->onNext(1);
  observingHandler->onNext(2);

  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Finish the broadcast
  observingHandler->onCompleted();
}

TEST_F(ObservingHandlerTest, ConnectError) {
  // Test when an error occurs while fetching the broadcast handler
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        // Inject error
        return makeFuture<BroadcastHandler<int>*>(
            make_exception_wrapper<std::exception>());
      }));
  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Initialize the pipeline
  pipeline->transportActive();
}

TEST_F(ObservingHandlerTest, ConnectHandlerDeletion) {
  // Test when the handler goes away before the broadcast handler
  // is obtained
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  Promise<BroadcastHandler<int>*> promise;
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([&] {
        return promise.getFuture();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  // Delete the handler and then fulfil the promise
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(0);
  pipeline.reset();
  promise.setValue(broadcastHandler.get());
}

TEST_F(ObservingHandlerTest, ConnectErrorHandlerDeletion) {
  // Test when an error occurs while fetching the broadcast handler
  // after the handler is deleted
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  Promise<BroadcastHandler<int>*> promise;
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([&] { return promise.getFuture(); }));

  // Initialize the pipeline
  pipeline->transportActive();

  // Delete the handler and then inject an error
  pipeline.reset();
  promise.setException(std::exception());
}

TEST_F(ObservingHandlerTest, BroadcastError) {
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<int>*>(std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _)).Times(1);

  // Broadcast some data
  observingHandler->onNext(1);

  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Inject broadcast error
  observingHandler->onError(make_exception_wrapper<std::exception>());
}

TEST_F(ObservingHandlerTest, ReadEOF) {
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<int>*>(std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _)).Times(1);

  // Broadcast some data
  observingHandler->onNext(1);

  EXPECT_CALL(*broadcastHandler, unsubscribe(_)).Times(1);
  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Client closes connection
  observingHandler->readEOF(nullptr);
}

TEST_F(ObservingHandlerTest, ReadError) {
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<int>*>(std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  EXPECT_CALL(*observingHandler, write(_, _)).Times(1);

  // Broadcast some data
  observingHandler->onNext(1);

  EXPECT_CALL(*broadcastHandler, unsubscribe(_)).Times(1);
  EXPECT_CALL(*observingHandler, close(_)).Times(1);

  // Inject read error
  observingHandler->readException(nullptr,
                                  make_exception_wrapper<std::exception>());
}

TEST_F(ObservingHandlerTest, WriteError) {
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<int>*>(std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
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
  observingHandler->onNext(1);
}

TEST_F(ObservingHandlerTest, WriteErrorHandlerDeletion) {
  // Test when write error occurs asynchronously after the handler
  // has been deleted.
  InSequence dummy;

  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));
  // Verify that ingress is paused
  EXPECT_CALL(*prevHandler, transportInactive(_)).WillOnce(Return());
  EXPECT_CALL(pool, getHandler(_))
      .WillOnce(InvokeWithoutArgs([this] {
        auto handler = broadcastHandler.get();
        return makeFuture<BroadcastHandler<int>*>(std::move(handler));
      }));
  EXPECT_CALL(*broadcastHandler, subscribe(_)).Times(1);
  // Verify that ingress is resumed
  EXPECT_CALL(*prevHandler, transportActive(_))
      .WillOnce(Invoke([&](MockBytesToBytesHandler::Context* ctx) {
        ctx->fireTransportActive();
      }));

  // Initialize the pipeline
  pipeline->transportActive();

  Promise<Unit> promise;
  EXPECT_CALL(*observingHandler, write(_, _))
      .WillOnce(InvokeWithoutArgs([&] { return promise.getFuture(); }));

  // Broadcast some data
  observingHandler->onNext(1);

  // Delete the pipeline and then fail the write
  EXPECT_CALL(*broadcastHandler, unsubscribe(_)).Times(1);
  pipeline.reset();
  promise.setException(std::exception());
}

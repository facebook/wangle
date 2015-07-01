// Copyright 2004-present Facebook.  All rights reserved.
#include <wangle/channel/broadcast/test/Mocks.h>

using namespace folly::wangle;
using namespace folly;
using namespace testing;

class BroadcastHandlerTest : public Test {
 public:
  class MockBroadcastHandler : public BroadcastHandler<std::string> {
   public:
    MOCK_METHOD2(processRead, bool(folly::IOBufQueue&, std::string&));
    MOCK_METHOD1(close, folly::Future<void>(Context*));
  };

  void SetUp() override {
    handler = new NiceMock<MockBroadcastHandler>();
  }

  void TearDown() override {
    Mock::VerifyAndClear(&subscriber0);
    Mock::VerifyAndClear(&subscriber1);
  }

 protected:
  NiceMock<MockBroadcastHandler>* handler{nullptr};
  StrictMock<MockSubscriber<std::string>> subscriber0;
  StrictMock<MockSubscriber<std::string>> subscriber1;
};

TEST_F(BroadcastHandlerTest, SubscribeUnsubscribe) {
  // Test by adding a couple of subscribers and unsubscribing them
  EXPECT_CALL(*handler, processRead(_, _))
      .WillRepeatedly(Invoke([&](IOBufQueue& q, std::string& data) {
        auto buf = q.move();
        buf->coalesce();
        data = buf->moveToFbString().toStdString();
        return true;
      }));

  InSequence dummy;

  // Add a subscriber
  EXPECT_EQ(handler->subscribe(&subscriber0), 0);

  EXPECT_CALL(subscriber0, onNext("data1")).Times(1);
  EXPECT_CALL(subscriber0, onNext("data2")).Times(1);

  // Push some data
  IOBufQueue q;
  q.append(IOBuf::copyBuffer("data1"));
  handler->read(nullptr, q);
  q.clear();
  q.append(IOBuf::copyBuffer("data2"));
  handler->read(nullptr, q);
  q.clear();

  // Add another subscriber
  EXPECT_EQ(handler->subscribe(&subscriber1), 1);

  EXPECT_CALL(subscriber0, onNext("data3")).Times(1);
  EXPECT_CALL(subscriber1, onNext("data3")).Times(1);

  // Push more data
  q.append(IOBuf::copyBuffer("data3"));
  handler->read(nullptr, q);
  q.clear();

  // Unsubscribe one of the subscribers
  handler->unsubscribe(0);

  EXPECT_CALL(subscriber1, onNext(Eq("data4"))).Times(1);

  // Push more data
  q.append(IOBuf::copyBuffer("data4"));
  handler->read(nullptr, q);
  q.clear();

  EXPECT_CALL(*handler, close(_))
      .WillOnce(InvokeWithoutArgs([this] {
        delete handler;
        return makeFuture();
      }));

  // Unsubscribe the other subscriber. The handler should be deleted now.
  handler->unsubscribe(1);
}

TEST_F(BroadcastHandlerTest, BufferedRead) {
  // Test with processRead that buffers data based on some local logic
  // before pushing to subscribers
  IOBufQueue bufQueue{IOBufQueue::cacheChainLength()};
  EXPECT_CALL(*handler, processRead(_, _))
      .WillRepeatedly(Invoke([&](IOBufQueue& q, std::string& data) {
        bufQueue.append(q);
        if (bufQueue.chainLength() < 5) {
          return false;
        }
        auto buf = bufQueue.move();
        buf->coalesce();
        data = buf->moveToFbString().toStdString();
        return true;
      }));

  InSequence dummy;

  // Add a subscriber
  EXPECT_EQ(handler->subscribe(&subscriber0), 0);

  EXPECT_CALL(subscriber0, onNext("data1")).Times(1);

  // Push some fragmented data
  IOBufQueue q;
  q.append(IOBuf::copyBuffer("da"));
  handler->read(nullptr, q);
  q.clear();
  q.append(IOBuf::copyBuffer("ta1"));
  handler->read(nullptr, q);
  q.clear();

  // Push more fragmented data. onNext shouldn't be called yet.
  q.append(IOBuf::copyBuffer("dat"));
  handler->read(nullptr, q);
  q.clear();
  q.append(IOBuf::copyBuffer("a"));
  handler->read(nullptr, q);
  q.clear();

  // Add another subscriber
  EXPECT_EQ(handler->subscribe(&subscriber1), 1);

  EXPECT_CALL(subscriber0, onNext("data3data4")).Times(1);
  EXPECT_CALL(subscriber1, onNext("data3data4")).Times(1);

  // Push rest of the fragmented data. The entire data should be pushed
  // to both subscribers.
  q.append(IOBuf::copyBuffer("3data4"));
  handler->read(nullptr, q);
  q.clear();

  EXPECT_CALL(subscriber0, onNext("data2")).Times(1);
  EXPECT_CALL(subscriber1, onNext("data2")).Times(1);

  // Push some unfragmented data
  q.append(IOBuf::copyBuffer("data2"));
  handler->read(nullptr, q);
  q.clear();

  EXPECT_CALL(*handler, close(_))
      .WillOnce(InvokeWithoutArgs([this] {
        delete handler;
        return makeFuture();
      }));

  // Unsubscribe all subscribers. The handler should be deleted now.
  handler->unsubscribe(0);
  handler->unsubscribe(1);
}

TEST_F(BroadcastHandlerTest, OnCompleted) {
  // Test with EOF on the handler
  EXPECT_CALL(*handler, processRead(_, _))
      .WillRepeatedly(Invoke([&](IOBufQueue& q, std::string& data) {
        auto buf = q.move();
        buf->coalesce();
        data = buf->moveToFbString().toStdString();
        return true;
      }));

  InSequence dummy;

  // Add a subscriber
  EXPECT_EQ(handler->subscribe(&subscriber0), 0);

  EXPECT_CALL(subscriber0, onNext("data1")).Times(1);

  // Push some data
  IOBufQueue q;
  q.append(IOBuf::copyBuffer("data1"));
  handler->read(nullptr, q);
  q.clear();

  // Add another subscriber
  EXPECT_EQ(handler->subscribe(&subscriber1), 1);

  EXPECT_CALL(subscriber0, onNext("data2")).Times(1);
  EXPECT_CALL(subscriber1, onNext("data2")).Times(1);

  // Push more data
  q.append(IOBuf::copyBuffer("data2"));
  handler->read(nullptr, q);
  q.clear();

  // Unsubscribe one of the subscribers
  handler->unsubscribe(0);

  EXPECT_CALL(subscriber1, onCompleted()).Times(1);

  EXPECT_CALL(*handler, close(_))
      .WillOnce(InvokeWithoutArgs([this] {
        delete handler;
        return makeFuture();
      }));

  // The handler should be deleted now
  handler->readEOF(nullptr);
}

TEST_F(BroadcastHandlerTest, OnError) {
  // Test with EOF on the handler
  EXPECT_CALL(*handler, processRead(_, _))
      .WillRepeatedly(Invoke([&](IOBufQueue& q, std::string& data) {
        auto buf = q.move();
        buf->coalesce();
        data = buf->moveToFbString().toStdString();
        return true;
      }));

  InSequence dummy;

  // Add a subscriber
  EXPECT_EQ(handler->subscribe(&subscriber0), 0);

  EXPECT_CALL(subscriber0, onNext("data1")).Times(1);

  // Push some data
  IOBufQueue q;
  q.append(IOBuf::copyBuffer("data1"));
  handler->read(nullptr, q);
  q.clear();

  // Add another subscriber
  EXPECT_EQ(handler->subscribe(&subscriber1), 1);

  EXPECT_CALL(subscriber0, onNext("data2")).Times(1);
  EXPECT_CALL(subscriber1, onNext("data2")).Times(1);

  // Push more data
  q.append(IOBuf::copyBuffer("data2"));
  handler->read(nullptr, q);
  q.clear();

  EXPECT_CALL(subscriber0, onError(_)).Times(1);
  EXPECT_CALL(subscriber1, onError(_)).Times(1);

  EXPECT_CALL(*handler, close(_))
      .WillOnce(InvokeWithoutArgs([this] {
        delete handler;
        return makeFuture();
      }));

  // The handler should be deleted now
  handler->readException(nullptr, make_exception_wrapper<std::exception>());
}

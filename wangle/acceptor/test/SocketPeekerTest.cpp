/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/SocketPeeker.h>

#include <thread>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/io/async/test/MockAsyncSocket.h>

using namespace folly;
using namespace folly::test;
using namespace wangle;
using namespace testing;

class MockSocketPeekerCallback : public SocketPeeker::Callback {
 public:
  ~MockSocketPeekerCallback() = default;

  GMOCK_METHOD1_(
      ,
      noexcept,
      ,
      peekSuccess,
      void(typename std::vector<uint8_t>));
  GMOCK_METHOD1_(
      ,
      noexcept,
      ,
      peekError,
      void(const folly::AsyncSocketException&));
};

class SocketPeekerTest : public Test {
  public:
    void SetUp() override {
      sock = new MockAsyncSocket(&base);
    }

    void TearDown() override {
      sock->destroy();
    }

    MockAsyncSocket* sock;
    MockSocketPeekerCallback callback;
    EventBase base;
};

MATCHER_P2(BufMatches, buf, len, "") {
  if (arg.size() != len) {
    return false;
  }
  return memcmp(buf, arg.data(), len) == 0;
}

TEST_F(SocketPeekerTest, TestPeekSuccess) {
  EXPECT_CALL(*sock, setReadCB(_));
  EXPECT_CALL(*sock, setPeek(true));
  SocketPeeker::UniquePtr peeker(new SocketPeeker(*sock, &callback, 2));
  peeker->start();

  uint8_t* buf = nullptr;
  size_t len = 0;
  peeker->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  buf[1] = 0x03;
  peeker->readDataAvailable(1);
  EXPECT_CALL(callback, peekSuccess(BufMatches(buf, 2)));
  // once after peeking, and once during destruction.
  EXPECT_CALL(*sock, setReadCB(nullptr));
  EXPECT_CALL(*sock, setPeek(false));
  peeker->readDataAvailable(2);
}

TEST_F(SocketPeekerTest, TestEOFDuringPeek) {
  EXPECT_CALL(*sock, setReadCB(_));
  EXPECT_CALL(*sock, setPeek(true));
  SocketPeeker::UniquePtr peeker(new SocketPeeker(*sock, &callback, 2));
  peeker->start();

  EXPECT_CALL(callback, peekError(_));
  EXPECT_CALL(*sock, setReadCB(nullptr));
  EXPECT_CALL(*sock, setPeek(false));
  peeker->readEOF();
}

TEST_F(SocketPeekerTest, TestErrAfterData) {
  EXPECT_CALL(*sock, setReadCB(_));
  EXPECT_CALL(*sock, setPeek(true));
  SocketPeeker::UniquePtr peeker(new SocketPeeker(*sock, &callback, 2));
  peeker->start();

  uint8_t* buf = nullptr;
  size_t len = 0;
  peeker->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  peeker->readDataAvailable(1);

  EXPECT_CALL(callback, peekError(_));
  EXPECT_CALL(*sock, setReadCB(nullptr));
  EXPECT_CALL(*sock, setPeek(false));
  peeker->readErr(AsyncSocketException(
        AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE,
          "Unit test"));
}

TEST_F(SocketPeekerTest, TestDestoryWhilePeeking) {
  EXPECT_CALL(*sock, setReadCB(_));
  EXPECT_CALL(*sock, setPeek(true));
  SocketPeeker::UniquePtr peeker(new SocketPeeker(*sock, &callback, 2));
  peeker->start();
  peeker = nullptr;
}

TEST_F(SocketPeekerTest, TestNoPeekSuccess) {
  SocketPeeker::UniquePtr peeker(new SocketPeeker(*sock, &callback, 0));

  char buf = '\0';
  EXPECT_CALL(callback, peekSuccess(BufMatches(&buf, 0)));
  peeker->start();
}

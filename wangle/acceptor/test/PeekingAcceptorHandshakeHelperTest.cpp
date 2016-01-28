/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/PeekingAcceptorHandshakeHelper.h>

#include <thread>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/io/async/test/MockAsyncSSLSocket.h>

using namespace folly;
using namespace folly::test;
using namespace wangle;
using namespace testing;

template<size_t N>
class MockPeekingCallback :
  public PeekingAcceptorHandshakeHelper<N>::PeekCallback {
  public:
    MOCK_METHOD5_T(getHelperInternal,
        AcceptorHandshakeHelper*(
            std::array<uint8_t, N>,
            Acceptor*,
            const folly::SocketAddress&,
            std::chrono::steady_clock::time_point,
            TransportInfo&));

    virtual AcceptorHandshakeHelper::UniquePtr getHelper(
        std::array<uint8_t, N> peekedBytes,
        Acceptor* acceptor,
        const folly::SocketAddress& clientAddr,
        std::chrono::steady_clock::time_point acceptTime,
        TransportInfo& tinfo) override {
      return AcceptorHandshakeHelper::UniquePtr(getHelperInternal(
            peekedBytes, acceptor, clientAddr, acceptTime, tinfo));
    }
};

class MockHandshakeHelperCallback : public AcceptorHandshakeHelper::Callback {
  public:

    GMOCK_METHOD3_(, noexcept, , connectionReadyInternal,
        void(
          std::shared_ptr<AsyncTransportWrapper> transport,
          std::string nextProtocol,
          SecureTransportType secureTransportType));

    virtual void connectionReady(
        folly::AsyncTransportWrapper::UniquePtr transport,
        std::string nextProtocol,
        SecureTransportType secureTransportType) noexcept override {
      connectionReadyInternal(
          std::shared_ptr<AsyncTransportWrapper>(std::move(transport)),
          nextProtocol,
          secureTransportType);
    }

    GMOCK_METHOD1_(, noexcept, , connectionError,
        void(const folly::exception_wrapper));
};

class MockHandshakeHelper : public AcceptorHandshakeHelper {
 public:
  GMOCK_METHOD2_(, noexcept, , startInternal,
      void(
        std::shared_ptr<AsyncSSLSocket> sock,
        AcceptorHandshakeHelper::Callback* callback));

  virtual void start(
      folly::AsyncSSLSocket::UniquePtr sock,
      AcceptorHandshakeHelper::Callback* callback) noexcept override {
    startInternal(std::shared_ptr<AsyncSSLSocket>(std::move(sock)), callback);
  }

  MOCK_METHOD1(dropConnection,
      void(
        SSLErrorEnum reason));

};

class PeekingAcceptorHandshakeHelperTest : public Test {
  protected:
    void SetUp() override {
      sslSock_ = new MockAsyncSSLSocket(
          SSLContextPtr(new SSLContext()),
          &base_,
          true /* defer security negotiation */);
      sockPtr_ = AsyncSSLSocket::UniquePtr(sslSock_);

      helper_ = new PeekingAcceptorHandshakeHelper<2>(
            nullptr,
            sa_,
            std::chrono::steady_clock::now(),
            tinfo_,
            &peekCallback_);

      innerHelper_ = new MockHandshakeHelper();
      helperPtr_ = AcceptorHandshakeHelper::UniquePtr(innerHelper_);
    }

    void TearDown() override {
      // Normally this would be destoryed by the AcceptorHandshakeManager.
      helper_->destroy();
    }

    PeekingAcceptorHandshakeHelper<2>* helper_;
    MockAsyncSSLSocket* sslSock_;
    AsyncSSLSocket::UniquePtr sockPtr_;
    EventBase base_;
    MockPeekingCallback<2> peekCallback_;
    MockHandshakeHelper* innerHelper_;
    AcceptorHandshakeHelper::UniquePtr helperPtr_;
    StrictMock<MockHandshakeHelperCallback> callback_;
    TransportInfo tinfo_;
    SocketAddress sa_;
};

TEST_F(PeekingAcceptorHandshakeHelperTest, TestPeekSuccess) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start(std::move(sockPtr_), &callback_);
  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  buf[1] = 0x03;
  helper_->readDataAvailable(1);
  EXPECT_CALL(peekCallback_, getHelperInternal(_, _, _, _, _))
    .WillOnce(Return(helperPtr_.release()));
  EXPECT_CALL(*innerHelper_, startInternal(_, _));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr));
  EXPECT_CALL(*sslSock_, setPeek(false));
  helper_->readDataAvailable(2);
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestPeekNonSuccess) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start(std::move(sockPtr_), &callback_);
  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  buf[1] = 0x03;
  helper_->readDataAvailable(1);
  EXPECT_CALL(peekCallback_, getHelperInternal(_, _, _, _, _))
    .WillOnce(Return(nullptr));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr))
    .Times(AtLeast(1));
  EXPECT_CALL(*sslSock_, setPeek(false));
  EXPECT_CALL(callback_, connectionError(_));
  helper_->readDataAvailable(2);
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestEOFDuringPeek) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start(std::move(sockPtr_), &callback_);
  EXPECT_CALL(callback_, connectionError(_));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr));
  helper_->readEOF();
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestErrAfterData) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start(std::move(sockPtr_), &callback_);

  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  helper_->readDataAvailable(1);

  EXPECT_CALL(callback_, connectionError(_));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr));
  helper_->readErr(AsyncSocketException(
        AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE,
          "Unit test"));
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestDropDuringPeek) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start(std::move(sockPtr_), &callback_);

  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  helper_->readDataAvailable(1);

  InSequence s;

  EXPECT_CALL(*sslSock_, closeNow());
  helper_->dropConnection();
  EXPECT_CALL(callback_, connectionError(_));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr));
  helper_->readErr(AsyncSocketException(
        AsyncSocketException::AsyncSocketExceptionType::UNKNOWN, "unit test"));
  EXPECT_CALL(*sslSock_, closeNow());
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestDropAfterPeek) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start(std::move(sockPtr_), &callback_);
  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  buf[1] = 0x03;

  EXPECT_CALL(peekCallback_, getHelperInternal(_, _, _, _, _))
    .WillOnce(Return(helperPtr_.release()));
  EXPECT_CALL(*innerHelper_, startInternal(_, _));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr));
  EXPECT_CALL(*sslSock_, setPeek(false));
  helper_->readDataAvailable(2);

  EXPECT_CALL(*innerHelper_, dropConnection(_));
  helper_->dropConnection();
}

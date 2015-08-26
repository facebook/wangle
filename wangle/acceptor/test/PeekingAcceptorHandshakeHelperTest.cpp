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
  public PeekingAcceptorHandshakeHelper<N>::Callback {
  public:
    MOCK_METHOD1_T(getSecureTransportType,
        Optional<SecureTransportType>(std::array<uint8_t, N>));
};

class MockAcceptor : public Acceptor {
  public:
    explicit MockAcceptor(const ServerSocketConfig& accConfig)
      : Acceptor(accConfig) {}

    MOCK_METHOD5(sslConnectionReadyInternal,
        void(
          std::shared_ptr<AsyncSocket> sock,
          const SocketAddress& clientAddr,
          const std::string& nextProtocol,
          SecureTransportType secureTransportType,
          TransportInfo& tinfo));

    MOCK_METHOD0(sslConnectionError, void());

    void sslConnectionReady(
        AsyncSocket::UniquePtr socket,
        const SocketAddress& clientAddr,
        const std::string& nextProtocol,
        SecureTransportType secureTransportType,
        TransportInfo& tinfo) override {
      sslConnectionReadyInternal(
          std::shared_ptr<AsyncSocket>(std::move(socket)),
          clientAddr,
          nextProtocol,
          secureTransportType,
          tinfo);
    }
};

class PeekingAcceptorHandshakeHelperTest : public Test {
  protected:
    void SetUp() override {
      sslSock_ = new MockAsyncSSLSocket(
          SSLContextPtr(new SSLContext()),
          &base_,
          true /* defer security negotiation */);
      EXPECT_CALL(*sslSock_, closeNow());
      auto sslSock = AsyncSSLSocket::UniquePtr(sslSock_);

      acceptor_.reset(new MockAcceptor(ServerSocketConfig()));
      acceptor_->init(nullptr, &base_);

      helper_ = new PeekingAcceptorHandshakeHelper<2>(
            std::move(sslSock),
            acceptor_.get(),
            SocketAddress(),
            std::chrono::steady_clock::now(),
            TransportInfo(),
            &peekCallback_);
    }

    PeekingAcceptorHandshakeHelper<2>* helper_;
    MockAsyncSSLSocket* sslSock_;
    EventBase base_;
    std::unique_ptr<MockAcceptor> acceptor_;
    MockPeekingCallback<2> peekCallback_;
};

TEST_F(PeekingAcceptorHandshakeHelperTest, TestNonSSLPeekSuccess) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start();
  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  buf[1] = 0x03;
  helper_->readDataAvailable(1);
  EXPECT_CALL(peekCallback_, getSecureTransportType(_))
    .WillOnce(Return(SecureTransportType::ZERO));
  EXPECT_CALL(*acceptor_, sslConnectionReadyInternal(_, _, _, _, _));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr));
  EXPECT_CALL(*sslSock_, setPeek(false));
  helper_->readDataAvailable(2);
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestSSLPeekSuccess) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start();
  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  buf[1] = 0x03;
  helper_->readDataAvailable(1);
  EXPECT_CALL(peekCallback_, getSecureTransportType(_))
    .WillOnce(Return(SecureTransportType::TLS));
  EXPECT_CALL(*sslSock_, sslAcceptMockable(_, _, _));
  EXPECT_CALL(*sslSock_, setReadCB(nullptr));
  EXPECT_CALL(*sslSock_, setPeek(false));
  helper_->readDataAvailable(2);
  helper_->destroy();
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestEOFDuringPeek) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start();
  EXPECT_CALL(*acceptor_, sslConnectionError());
  helper_->readEOF();
}

TEST_F(PeekingAcceptorHandshakeHelperTest, TestErrAfterData) {
  EXPECT_CALL(*sslSock_, setReadCB(_));
  EXPECT_CALL(*sslSock_, setPeek(true));
  helper_->start();

  uint8_t* buf = nullptr;
  size_t len = 0;
  helper_->getReadBuffer(reinterpret_cast<void**>(&buf), &len);
  EXPECT_EQ(2, len);
  // first 2 bytes of SSL3+.
  buf[0] = 0x16;
  helper_->readDataAvailable(1);

  EXPECT_CALL(*acceptor_, sslConnectionError());
  helper_->readErr(AsyncSocketException(
        AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE,
          "Unit test"));
}

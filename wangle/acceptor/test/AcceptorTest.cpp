/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/acceptor/Acceptor.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/test/AsyncSSLSocketTest.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <glog/logging.h>
#include <wangle/acceptor/AcceptObserver.h>

using namespace folly;
using namespace wangle;
using namespace testing;

class TestConnection : public wangle::ManagedConnection {
 public:
  void timeoutExpired() noexcept override {}
  void describe(std::ostream& /*os*/) const override {}
  bool isBusy() const override {
    return false;
  }
  void notifyPendingShutdown() override {}
  void closeWhenIdle() override {}
  void dropConnection(const std::string& /* errorMsg */ = "") override {
    delete this;
  }
  void dumpConnectionState(uint8_t /*loglevel*/) override {}
};

class TestAcceptor : public Acceptor {
 public:
  explicit TestAcceptor(const ServerSocketConfig& accConfig)
      : Acceptor(accConfig) {}

  void onNewConnection(
      folly::AsyncTransportWrapper::UniquePtr /*sock*/,
      const folly::SocketAddress* /*address*/,
      const std::string& /*nextProtocolName*/,
      SecureTransportType /*secureTransportType*/,
      const TransportInfo& /*tinfo*/) override {
    addConnection(new TestConnection);
    getEventBase()->terminateLoopSoon();
  }
};

enum class TestSSLConfig { NO_SSL, SSL };

class AcceptorTest : public ::testing::TestWithParam<TestSSLConfig> {
 public:
  AcceptorTest() = default;

  std::shared_ptr<AsyncSocket> connectClientSocket(
      const SocketAddress& serverAddress) {
    TestSSLConfig testConfig = GetParam();
    if (testConfig == TestSSLConfig::SSL) {
      auto clientSocket = AsyncSSLSocket::newSocket(getTestSslContext(), &evb_);
      clientSocket->connect(nullptr, serverAddress);
      return clientSocket;
    } else {
      return AsyncSocket::newSocket(&evb_, serverAddress);
    }
  }

  std::tuple<std::shared_ptr<TestAcceptor>, std::shared_ptr<AsyncServerSocket>>
  initTestAcceptorAndSocket() {
    TestSSLConfig testConfig = GetParam();
    ServerSocketConfig config;
    if (testConfig == TestSSLConfig::SSL) {
      config.sslContextConfigs.emplace_back(getTestSslContextConfig());
    }
    return initTestAcceptorAndSocket(config);
  }

  std::tuple<std::shared_ptr<TestAcceptor>, std::shared_ptr<AsyncServerSocket>>
  initTestAcceptorAndSocket(ServerSocketConfig config) {
    auto acceptor = std::make_shared<TestAcceptor>(config);
    auto socket = AsyncServerSocket::newSocket(&evb_);
    socket->addAcceptCallback(acceptor.get(), &evb_);
    acceptor->init(socket.get(), &evb_);
    socket->bind(0);
    socket->listen(100);
    socket->startAccepting();
    return std::make_tuple(acceptor, socket);
  }

  static std::shared_ptr<folly::SSLContext> getTestSslContext() {
    auto sslContext = std::make_shared<folly::SSLContext>();
    sslContext->setOptions(SSL_OP_NO_TICKET);
    sslContext->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
    return sslContext;
  }

  static wangle::SSLContextConfig getTestSslContextConfig() {
    wangle::SSLContextConfig sslCtxConfig;
    sslCtxConfig.setCertificate(folly::kTestCert, folly::kTestKey, "");
    sslCtxConfig.clientCAFile = folly::kTestCA;
    sslCtxConfig.sessionContext = "AcceptorTest";
    sslCtxConfig.isDefault = true;
    sslCtxConfig.clientVerification =
        folly::SSLContext::SSLVerifyPeerEnum::NO_VERIFY;
    sslCtxConfig.sessionCacheEnabled = false;
    return sslCtxConfig;
  }

 protected:
  EventBase evb_;
};

INSTANTIATE_TEST_CASE_P(
    NoSSLAndSSLTests,
    AcceptorTest,
    ::testing::Values(TestSSLConfig::NO_SSL, TestSSLConfig::SSL));

TEST_P(AcceptorTest, Basic) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  SocketAddress serverAddress;
  serverSocket->getAddress(&serverAddress);
  auto clientSocket = connectClientSocket(serverAddress);

  evb_.loopForever();

  CHECK_EQ(acceptor->getNumConnections(), 1);
  CHECK(acceptor->getState() == Acceptor::State::kRunning);
  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();
}

class MockAcceptObserver : public AcceptObserver {
 public:
  GMOCK_METHOD1_(, noexcept, , accept, void(folly::AsyncTransport* const));
  GMOCK_METHOD1_(, noexcept, , ready, void(folly::AsyncTransport* const));
  GMOCK_METHOD1_(, noexcept, , acceptorDestroy, void(Acceptor* const));
  GMOCK_METHOD1_(, noexcept, , observerAttach, void(Acceptor* const));
  GMOCK_METHOD1_(, noexcept, , observerDetach, void(Acceptor* const));
};

TEST_P(AcceptorTest, AcceptObserver) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  SocketAddress serverAddress;
  serverSocket->getAddress(&serverAddress);

  auto cb = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb.get());

  // add first connection, expect callbacks
  auto clientSocket1 = connectClientSocket(serverAddress);
  {
    InSequence s;
    EXPECT_CALL(*cb, accept(_));
    EXPECT_CALL(*cb, ready(_));
  }
  evb_.loopForever();
  Mock::VerifyAndClearExpectations(cb.get());
  CHECK_EQ(acceptor->getNumConnections(), 1);
  CHECK(acceptor->getState() == Acceptor::State::kRunning);

  // add second connection, expect callbacks
  auto clientSocket2 = connectClientSocket(serverAddress);
  {
    InSequence s;
    EXPECT_CALL(*cb, accept(_));
    EXPECT_CALL(*cb, ready(_));
  }
  evb_.loopForever();
  Mock::VerifyAndClearExpectations(cb.get());
  CHECK_EQ(acceptor->getNumConnections(), 2);
  CHECK(acceptor->getState() == Acceptor::State::kRunning);

  // remove AcceptObserver
  EXPECT_CALL(*cb, observerDetach(acceptor.get()));
  EXPECT_TRUE(acceptor->removeAcceptObserver(cb.get()));
  Mock::VerifyAndClearExpectations(cb.get());

  // add third connection, no callbacks
  auto clientSocket3 = connectClientSocket(serverAddress);
  evb_.loopForever();
  Mock::VerifyAndClearExpectations(cb.get());
  CHECK_EQ(acceptor->getNumConnections(), 3);
  CHECK(acceptor->getState() == Acceptor::State::kRunning);

  // stop the acceptor
  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();
}

TEST_P(AcceptorTest, AcceptObserverRemove) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  auto cb = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb.get());
  Mock::VerifyAndClearExpectations(cb.get());

  EXPECT_CALL(*cb, observerDetach(acceptor.get()));
  EXPECT_TRUE(acceptor->removeAcceptObserver(cb.get()));
  Mock::VerifyAndClearExpectations(cb.get());

  // cleanup
  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();
}

TEST_P(AcceptorTest, AcceptObserverRemoveMissing) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  auto cb = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_FALSE(acceptor->removeAcceptObserver(cb.get()));

  // cleanup
  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();
}

TEST_P(AcceptorTest, AcceptObserverAcceptorDestroyed) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  auto cb = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb.get());
  Mock::VerifyAndClearExpectations(cb.get());

  // stop the acceptor
  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();

  // destroy the acceptor while the AcceptObserver is installed
  EXPECT_CALL(*cb, acceptorDestroy(acceptor.get()));
  acceptor = nullptr;
  Mock::VerifyAndClearExpectations(cb.get());
}

TEST_P(AcceptorTest, AcceptObserverMultipleRemove) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  auto cb1 = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb1, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb1.get());
  Mock::VerifyAndClearExpectations(cb1.get());

  auto cb2 = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb2, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb2.get());
  Mock::VerifyAndClearExpectations(cb1.get());
  Mock::VerifyAndClearExpectations(cb2.get());

  EXPECT_CALL(*cb2, observerDetach(acceptor.get()));
  EXPECT_TRUE(acceptor->removeAcceptObserver(cb2.get()));
  Mock::VerifyAndClearExpectations(cb1.get());
  Mock::VerifyAndClearExpectations(cb2.get());

  EXPECT_CALL(*cb1, observerDetach(acceptor.get()));
  EXPECT_TRUE(acceptor->removeAcceptObserver(cb1.get()));
  Mock::VerifyAndClearExpectations(cb1.get());
  Mock::VerifyAndClearExpectations(cb2.get());

  // cleanup
  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();
}

TEST_P(AcceptorTest, AcceptObserverMultipleAcceptorDestroyed) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  auto cb1 = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb1, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb1.get());
  Mock::VerifyAndClearExpectations(cb1.get());

  auto cb2 = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb2, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb2.get());
  Mock::VerifyAndClearExpectations(cb1.get());
  Mock::VerifyAndClearExpectations(cb2.get());

  // stop the acceptor
  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();

  // destroy the acceptor while the AcceptObserver is installed
  EXPECT_CALL(*cb1, acceptorDestroy(acceptor.get()));
  EXPECT_CALL(*cb2, acceptorDestroy(acceptor.get()));
  acceptor = nullptr;
  Mock::VerifyAndClearExpectations(cb1.get());
  Mock::VerifyAndClearExpectations(cb2.get());
}

TEST_P(AcceptorTest, AcceptObserverRemoveCallbackThenStopAcceptor) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  auto cb = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb.get());
  Mock::VerifyAndClearExpectations(cb.get());

  EXPECT_CALL(*cb, observerDetach(acceptor.get()));
  EXPECT_TRUE(acceptor->removeAcceptObserver(cb.get()));
  Mock::VerifyAndClearExpectations(cb.get());

  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();
}

TEST_P(AcceptorTest, AcceptObserverStopAcceptorThenRemoveCallback) {
  auto [acceptor, serverSocket] = initTestAcceptorAndSocket();
  auto cb = std::make_unique<StrictMock<MockAcceptObserver>>();
  EXPECT_CALL(*cb, observerAttach(acceptor.get()));
  acceptor->addAcceptObserver(cb.get());
  Mock::VerifyAndClearExpectations(cb.get());

  acceptor->forceStop();
  serverSocket->stopAccepting();
  evb_.loop();

  EXPECT_CALL(*cb, observerDetach(acceptor.get()));
  EXPECT_TRUE(acceptor->removeAcceptObserver(cb.get()));
  Mock::VerifyAndClearExpectations(cb.get());
}

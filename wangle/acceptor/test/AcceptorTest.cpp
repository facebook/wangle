/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/Acceptor.h>

#include <gtest/gtest.h>

using namespace folly;
using namespace testing;

namespace wangle {

class SimpleConnectionCounterForTest : public SimpleConnectionCounter {
  public:
    void setNumConnections(const uint64_t numConnections) {
      numConnections_ = numConnections;
    }
};

class TestableAcceptor : public Acceptor {
  public:
    explicit TestableAcceptor(const ServerSocketConfig& accConfig) :
      Acceptor(accConfig) {}
    virtual ~TestableAcceptor() {}

    void setActiveConnectionCountForLoadShedding(
        const uint64_t activeConnectionCountForLoadShedding) {
      activeConnectionCountForLoadShedding_ =
        activeConnectionCountForLoadShedding;
    }

    void setConnectionCountForLoadShedding(
        const uint64_t connectionCountForLoadShedding) {
      connectionCountForLoadShedding_ = connectionCountForLoadShedding;
    }

    using Acceptor::setLoadShedConfig;
    using Acceptor::canAccept;

  protected:
    virtual uint64_t getConnectionCountForLoadShedding() const override {
      return connectionCountForLoadShedding_;
    }
    virtual uint64_t getActiveConnectionCountForLoadShedding() const override {
      return activeConnectionCountForLoadShedding_;
    }

  private:
    uint64_t connectionCountForLoadShedding_{0};
    uint64_t activeConnectionCountForLoadShedding_{0};
};

class AcceptorTest : public Test {
  protected:
    void SetUp() override {
      acceptor_.setLoadShedConfig(loadShedConfig_, &connectionCounter_);
    }

    SocketAddress address_ { "127.0.0.1", 2000 };
    TestableAcceptor acceptor_ { ServerSocketConfig() };
    LoadShedConfiguration loadShedConfig_;
    SimpleConnectionCounterForTest connectionCounter_;
};

TEST_F(AcceptorTest, TestCanAcceptWithNoConnectionCounter) {
  acceptor_.setLoadShedConfig(loadShedConfig_, nullptr);
  // Should accept if there is no IConnectionCounter set
  EXPECT_TRUE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWithMaxConnectionsZero) {
  // Should accept if maxConnections is zero
  connectionCounter_.setMaxConnections(0);
  EXPECT_TRUE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWithCurrentConnsLessThanMax) {
  // Should accept if currentConnections is less than maxConnections
  connectionCounter_.setNumConnections(100);
  connectionCounter_.setMaxConnections(200);
  EXPECT_TRUE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWithCurrentConnsGreaterThanMax) {
  // Should not accept if currentConnections is larger than maxConnections
  connectionCounter_.setNumConnections(300);
  connectionCounter_.setMaxConnections(200);
  EXPECT_FALSE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWhiteListedAddress) {
  // Should accept if currentConnections is larger than maxConnections
  // but the address is whitelisted
  connectionCounter_.setNumConnections(300);
  connectionCounter_.setMaxConnections(200);
  LoadShedConfiguration::AddressSet addrs = { address_ };
  loadShedConfig_.setWhitelistAddrs(addrs);
  acceptor_.setLoadShedConfig(loadShedConfig_, &connectionCounter_);
  EXPECT_TRUE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWithNoLoadShed) {
  // Should accept if currentConnections is larger than maxConnections,
  // the address is not whitelisted and the current active and total connections
  // counts are below the corresponding thresholds
  connectionCounter_.setNumConnections(300);
  connectionCounter_.setMaxConnections(200);
  loadShedConfig_.setMaxActiveConnections(100);
  loadShedConfig_.setMaxConnections(200);
  acceptor_.setLoadShedConfig(loadShedConfig_, &connectionCounter_);
  EXPECT_TRUE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWithActiveConnectionsBreachingThreshold) {
  // Should not accept if currentConnections is larger than maxConnections,
  // the address is not whitelisted and the current active connection is larger
  // than the threshold
  connectionCounter_.setNumConnections(300);
  connectionCounter_.setMaxConnections(200);
  loadShedConfig_.setMaxActiveConnections(100);
  loadShedConfig_.setMaxConnections(200);
  acceptor_.setLoadShedConfig(loadShedConfig_, &connectionCounter_);
  acceptor_.setActiveConnectionCountForLoadShedding(110);
  EXPECT_FALSE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWithTotalConnectionsBreachingThreshold) {
  // Should not accept if currentConnections is larger than maxConnections,
  // the address is not whitelisted and the current connection is larger
  // than the threshold
  connectionCounter_.setNumConnections(300);
  connectionCounter_.setMaxConnections(200);
  loadShedConfig_.setMaxActiveConnections(100);
  loadShedConfig_.setMaxConnections(200);
  acceptor_.setLoadShedConfig(loadShedConfig_, &connectionCounter_);
  acceptor_.setConnectionCountForLoadShedding(210);
  EXPECT_FALSE(acceptor_.canAccept(address_));
}

TEST_F(AcceptorTest, TestCanAcceptWithBothConnectionCountsBreachingThresholds) {
  // Should not accept if currentConnections is larger than maxConnections,
  // the address is not whitelisted and the both current active and total
  // connections counts are larger than the corresponding thresholds
  connectionCounter_.setNumConnections(300);
  connectionCounter_.setMaxConnections(200);
  loadShedConfig_.setMaxActiveConnections(100);
  loadShedConfig_.setMaxConnections(200);
  acceptor_.setLoadShedConfig(loadShedConfig_, &connectionCounter_);
  acceptor_.setActiveConnectionCountForLoadShedding(110);
  acceptor_.setConnectionCountForLoadShedding(210);
  EXPECT_FALSE(acceptor_.canAccept(address_));
}

} // namespace wangle

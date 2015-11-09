/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/ConnectionManager.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace folly;
using namespace testing;
using namespace wangle;

namespace {

class MockConnection : public ManagedConnection {
 public:

  MockConnection() {
    EXPECT_CALL(*this, isBusy())
      .WillRepeatedly(Return(false));
    EXPECT_CALL(*this, dumpConnectionState(testing::_))
      .WillRepeatedly(Return());
  }

  GMOCK_METHOD0_(, noexcept,, timeoutExpired, void());

  MOCK_CONST_METHOD1(describe, void(std::ostream& os));

  MOCK_CONST_METHOD0(isBusy, bool());

  MOCK_CONST_METHOD0(getIdleTime, std::chrono::milliseconds());

  MOCK_METHOD0(notifyPendingShutdown, void());
  MOCK_METHOD0(closeWhenIdle, void());
  MOCK_METHOD0(dropConnection, void());
  MOCK_METHOD1(dumpConnectionState, void(uint8_t));
};

class ConnectionManagerTest: public testing::Test {

 public:
  ConnectionManagerTest() {
    cm_= ConnectionManager::makeUnique(&eventBase_,
                                       std::chrono::milliseconds(100),
                                       nullptr);
    addConns(65);
  }

  void SetUp() override {
  }

  void addConns(uint64_t n) {
    for (auto i = 0; i < n; i++) {
      conns_.insert(conns_.begin(),
                    folly::make_unique<StrictMock<MockConnection>>());
      cm_->addConnection(conns_.front().get());
    }
  }

 protected:
  folly::EventBase eventBase_;
  ConnectionManager::UniquePtr cm_;
  std::vector<std::unique_ptr<MockConnection>> conns_;
};

TEST_F(ConnectionManagerTest, testShutdownSequence) {
  InSequence enforceOrder;

  // activate one connection, it should not be exempt from notifyPendingShutdown
  cm_->onActivated(*conns_.front());
  // make sure the idleIterator points to !end
  cm_->onDeactivated(*conns_.back());
  for (const auto& conn: conns_) {
    EXPECT_CALL(*conn, notifyPendingShutdown());
  }
  cm_->initiateGracefulShutdown(std::chrono::milliseconds(50));
  eventBase_.loopOnce();
  for (const auto& conn: conns_) {
    EXPECT_CALL(*conn, closeWhenIdle());
  }

  eventBase_.loop();
}

TEST_F(ConnectionManagerTest, testRemoveDrainIterator) {
  addConns(1);
  InSequence enforceOrder;

  // activate one connection, it should not be exempt from notifyPendingShutdown
  cm_->onActivated(*conns_.front());
  for (auto i = 0; i < conns_.size() - 1; i++) {
    EXPECT_CALL(*conns_[i], notifyPendingShutdown());
  }
  auto conn65 = conns_[conns_.size() - 2].get();
  auto conn66 = conns_[conns_.size() - 1].get();
  eventBase_.runInLoop([&] {
      // deactivate the drain iterator
      cm_->onDeactivated(*conn65);
      // remove the drain iterator
      cm_->removeConnection(conn66);
      // deactivate the new drain iterator, now it's the end of the list
      cm_->onDeactivated(*conn65);
    });
  cm_->initiateGracefulShutdown(std::chrono::milliseconds(50));
  // Schedule a loop callback to remove the connection pointed to by the drain
  // iterator
  eventBase_.loopOnce();
  for (auto i = 0; i < conns_.size() - 1; i++) {
    EXPECT_CALL(*conns_[i], closeWhenIdle());
  }

  eventBase_.loop();
}

TEST_F(ConnectionManagerTest, testIdleGraceTimeout) {
  InSequence enforceOrder;

  // Slow down the notifyPendingShutdown calls enough so that the idle grace
  // timeout fires before the end of the loop.
  // I would prefer a non-sleep solution to this, but I can't think how to do it
  // without changing the class to expose internal details
  for (const auto& conn: conns_) {
    EXPECT_CALL(*conn, notifyPendingShutdown())
      .WillOnce(Invoke([] { /* sleep override */ usleep(1000); }));
  }
  cm_->initiateGracefulShutdown(std::chrono::milliseconds(1));
  eventBase_.loopOnce();
  for (const auto& conn: conns_) {
    EXPECT_CALL(*conn, closeWhenIdle());
  }

  eventBase_.loop();
}

TEST_F(ConnectionManagerTest, testDropAll) {
  InSequence enforceOrder;

  for (const auto& conn: conns_) {
    EXPECT_CALL(*conn, dropConnection())
      .WillOnce(Invoke([&] { cm_->removeConnection(conn.get()); }));
  }
  cm_->dropAllConnections();
}

TEST_F(ConnectionManagerTest, testDropIdle) {
  for (const auto& conn: conns_) {
    // Set everyone to be idle for 100ms
    EXPECT_CALL(*conn, getIdleTime())
      .WillRepeatedly(Return(std::chrono::milliseconds(100)));
  }

  // Mark the first half of the connections idle
  for (auto i = 0; i < conns_.size() / 2; i++) {
    cm_->onDeactivated(*conns_[i]);
  }
  // reactivate conn 0
  cm_->onActivated(*conns_[0]);
  // remove the first idle conn
  cm_->removeConnection(conns_[1].get());

  InSequence enforceOrder;

  // Expect the remaining idle conns to drop
  for (auto i = 2; i < conns_.size() / 2; i++) {
    EXPECT_CALL(*conns_[i], timeoutExpired())
      .WillOnce(Invoke([&] { cm_->removeConnection(conns_[i].get()); }));
  }

  cm_->dropIdleConnections(conns_.size());
}

TEST_F(ConnectionManagerTest, testAddDuringShutdown) {
  auto extraConn = folly::make_unique<StrictMock<MockConnection>>();
  InSequence enforceOrder;

  // activate one connection, it should not be exempt from notifyPendingShutdown
  cm_->onActivated(*conns_.front());
  for (const auto& conn: conns_) {
    EXPECT_CALL(*conn, notifyPendingShutdown());
  }
  cm_->initiateGracefulShutdown(std::chrono::milliseconds(50));
  eventBase_.loopOnce();
  conns_.insert(conns_.begin(), std::move(extraConn));
  EXPECT_CALL(*conns_.front(), notifyPendingShutdown());
  cm_->addConnection(conns_.front().get());
  for (const auto& conn: conns_) {
    EXPECT_CALL(*conn, closeWhenIdle());
  }

  eventBase_.loop();
}
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  return RUN_ALL_TESTS();
}

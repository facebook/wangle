/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/SecurityProtocolContextManager.h>

#include <thread>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace wangle;
using namespace testing;

template<size_t N>
class LengthPeeker :
  public PeekingAcceptorHandshakeHelper::PeekCallback {
 public:
  LengthPeeker():
    PeekingAcceptorHandshakeHelper::PeekCallback(N) {}

  AcceptorHandshakeHelper::UniquePtr getHelper(
      const std::vector<uint8_t>& /* bytes */,
      Acceptor* acceptor,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      TransportInfo& tinfo) override {
    return nullptr;
  }
};

class SecurityProtocolContextManagerTest : public Test {
 protected:
  SecurityProtocolContextManager manager_;
  LengthPeeker<0> p0_;
  LengthPeeker<2> p2_;
  LengthPeeker<4> p4_;
  LengthPeeker<9> p9_;
};

TEST_F(SecurityProtocolContextManagerTest, TestZeroLen) {
  manager_.addPeeker(&p0_);

  EXPECT_EQ(manager_.getPeekBytes(), 0);
}

TEST_F(SecurityProtocolContextManagerTest, TestLongAtStart) {
  manager_.addPeeker(&p9_);
  manager_.addPeeker(&p0_);
  manager_.addPeeker(&p4_);
  manager_.addPeeker(&p2_);

  EXPECT_EQ(manager_.getPeekBytes(), 9);
}

TEST_F(SecurityProtocolContextManagerTest, TestLongAtEnd) {
  manager_.addPeeker(&p0_);
  manager_.addPeeker(&p4_);
  manager_.addPeeker(&p2_);
  manager_.addPeeker(&p9_);

  EXPECT_EQ(manager_.getPeekBytes(), 9);
}

TEST_F(SecurityProtocolContextManagerTest, TestLongMiddle) {
  manager_.addPeeker(&p0_);
  manager_.addPeeker(&p9_);
  manager_.addPeeker(&p2_);
  manager_.addPeeker(&p0_);

  EXPECT_EQ(manager_.getPeekBytes(), 9);
}

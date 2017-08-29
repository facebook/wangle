/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <wangle/acceptor/LoadShedConfiguration.h>

#include <gtest/gtest.h>

using namespace wangle;
using namespace testing;

TEST(LoadShedConfigurationTest, TestSettersAndGetters) {
  LoadShedConfiguration lsc;

  lsc.setMaxConnections(10);
  EXPECT_EQ(10, lsc.getMaxConnections());

  lsc.setMaxActiveConnections(20);
  EXPECT_EQ(20, lsc.getMaxActiveConnections());

  EXPECT_EQ(0, lsc.getAcceptPauseOnAcceptorQueueSize());
  lsc.setAcceptPauseOnAcceptorQueueSize(40);
  EXPECT_EQ(40, lsc.getAcceptPauseOnAcceptorQueueSize());

  EXPECT_EQ(0, lsc.getAcceptResumeOnAcceptorQueueSize());
  lsc.setAcceptResumeOnAcceptorQueueSize(50);
  EXPECT_EQ(50, lsc.getAcceptResumeOnAcceptorQueueSize());

  lsc.setMinFreeMem(30);
  EXPECT_EQ(30, lsc.getMinFreeMem());

  lsc.setMaxMemUsage(0.1);
  EXPECT_EQ(0.1, lsc.getMaxMemUsage());

  lsc.setMaxCpuUsage(0.2);
  EXPECT_EQ(0.2, lsc.getMaxCpuUsage());

  lsc.setMinCpuIdle(0.03);
  EXPECT_EQ(0.03, lsc.getMinCpuIdle());

  EXPECT_EQ(0, lsc.getCpuUsageExceedWindowSize());
  lsc.setCpuUsageExceedWindowSize(12);
  EXPECT_EQ(12, lsc.getCpuUsageExceedWindowSize());

  lsc.setLoadUpdatePeriod(std::chrono::milliseconds(1200));
  EXPECT_EQ(std::chrono::milliseconds(1200), lsc.getLoadUpdatePeriod());

  LoadShedConfiguration::AddressSet addressSet = {
    folly::SocketAddress("127.0.0.1", 1100),
    folly::SocketAddress("127.0.0.2", 1200),
    folly::SocketAddress("127.0.0.3", 1300),
  };
  lsc.setWhitelistAddrs(addressSet);

  EXPECT_EQ(addressSet, lsc.getWhitelistAddrs());
  EXPECT_TRUE(lsc.isWhitelisted(folly::SocketAddress("127.0.0.1", 1100)));
  EXPECT_TRUE(lsc.isWhitelisted(folly::SocketAddress("127.0.0.2", 1200)));
  EXPECT_TRUE(lsc.isWhitelisted(folly::SocketAddress("127.0.0.3", 1300)));
  EXPECT_FALSE(lsc.isWhitelisted(folly::SocketAddress("127.0.0.4", 1400)));
  lsc.addWhitelistAddr(folly::StringPiece("127.0.0.4"));
  EXPECT_TRUE(lsc.isWhitelisted(folly::SocketAddress("127.0.0.4", 0)));

  LoadShedConfiguration::NetworkSet networkSet = {
    NetworkAddress(folly::SocketAddress("127.0.0.5", 1500), 28),
    NetworkAddress(folly::SocketAddress("127.0.0.6", 1600), 24),
  };
  lsc.setWhitelistNetworks(networkSet);
  EXPECT_EQ(networkSet, lsc.getWhitelistNetworks());
  EXPECT_TRUE(lsc.isWhitelisted(folly::SocketAddress("127.0.0.5", 1500)));
  EXPECT_TRUE(lsc.isWhitelisted(folly::SocketAddress("127.0.0.6", 1300)));
  EXPECT_FALSE(lsc.isWhitelisted(folly::SocketAddress("10.0.0.7", 1700)));
  lsc.addWhitelistAddr(folly::StringPiece("10.0.0.7/20"));
  EXPECT_TRUE(lsc.isWhitelisted(folly::SocketAddress("10.0.0.7", 0)));
}

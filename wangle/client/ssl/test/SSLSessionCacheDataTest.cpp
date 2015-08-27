// Copyright 2004-present Facebook.  All rights reserved.
#include <chrono>
#include <folly/DynamicConverter.h>
#include <gtest/gtest.h>
#include <wangle/client/ssl/SSLSessionCacheData.h>
#include <vector>


using namespace testing;
using namespace std::chrono;

using wangle::SSLSessionCacheData;

class SSLSessionCacheDataTest : public Test {
 public:
  void SetUp() override {

  }

  void TearDown() override {
  }

};

TEST_F(SSLSessionCacheDataTest, Basic) {
  SSLSessionCacheData data;
  data.sessionData = folly::fbstring("some session data");
  data.addedTime = system_clock::now();

  auto d = folly::toDynamic(data);
  auto deserializedData = folly::convertTo<SSLSessionCacheData>(d);

  EXPECT_EQ(deserializedData.sessionData, data.sessionData);
  EXPECT_EQ(deserializedData.addedTime, data.addedTime);
}

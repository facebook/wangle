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
#include <chrono>
#include <vector>

#include <folly/DynamicConverter.h>
#include <gtest/gtest.h>
#include <wangle/client/ssl/SSLSession.h>
#include <wangle/client/ssl/SSLSessionCacheData.h>
#include <wangle/client/ssl/SSLSessionCacheUtils.h>
#include <wangle/client/ssl/test/TestUtil.h>
#include <wangle/ssl/SSLUtil.h>

using namespace std::chrono;
using namespace testing;
using namespace wangle;

using SSLCtxDeleter = folly::static_function_deleter<SSL_CTX, &SSL_CTX_free>;
using SSLCtxPtr = std::unique_ptr<SSL_CTX, SSLCtxDeleter>;

class SSLSessionCacheDataTest : public Test {
 public:
  void SetUp() override {
    sessions_ = getSessions();
  }

  void TearDown() override {
    for (const auto& it : sessions_) {
      SSL_SESSION_free(it.first);
    }
    sessions_.clear();
  }

 protected:
  std::vector<std::pair<SSL_SESSION*, size_t>> sessions_;
};

TEST_F(SSLSessionCacheDataTest, Basic) {
  SSLSessionCacheData data;
  data.sessionData = folly::fbstring("some session data");
  data.addedTime = system_clock::now();
  data.serviceIdentity = "some service";

  auto d = folly::toDynamic(data);
  auto deserializedData = folly::convertTo<SSLSessionCacheData>(d);

  EXPECT_EQ(deserializedData.sessionData, data.sessionData);
  EXPECT_EQ(deserializedData.addedTime, data.addedTime);
  EXPECT_EQ(deserializedData.serviceIdentity, data.serviceIdentity);
}

TEST_F(SSLSessionCacheDataTest, CloneSSLSession) {
  for (auto& it : sessions_) {
    auto sess = SSLSessionPtr(cloneSSLSession(it.first));
    EXPECT_TRUE(sess);
  }
}

TEST_F(SSLSessionCacheDataTest, ServiceIdentity) {
  auto sessionPtr = SSLSessionPtr(cloneSSLSession(sessions_[0].first));
  auto session = sessionPtr.get();
  auto ident = getSessionServiceIdentity(session);
  EXPECT_FALSE(ident);

  std::string id("serviceId");
  EXPECT_TRUE(setSessionServiceIdentity(session, id));
  ident = getSessionServiceIdentity(session);
  EXPECT_TRUE(ident);
  EXPECT_EQ(ident.value(), id);

  auto cloned = SSLSessionPtr(cloneSSLSession(session));
  EXPECT_TRUE(cloned);
  ident = getSessionServiceIdentity(cloned.get());
  EXPECT_TRUE(ident);
  EXPECT_EQ(ident.value(), id);

  auto cacheDataOpt = getCacheDataForSession(session);
  EXPECT_TRUE(cacheDataOpt);
  auto& cacheData = cacheDataOpt.value();
  EXPECT_EQ(id, cacheData.serviceIdentity);

  auto deserialized = SSLSessionPtr(getSessionFromCacheData(cacheData));
  EXPECT_TRUE(deserialized);
  ident = getSessionServiceIdentity(deserialized.get());
  EXPECT_TRUE(ident);
  EXPECT_EQ(ident.value(), id);
}

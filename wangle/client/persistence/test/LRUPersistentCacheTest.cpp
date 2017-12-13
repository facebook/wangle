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
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <folly/synchronization/Baton.h>
#include <folly/Memory.h>
#include <folly/futures/Future.h>
#include <wangle/client/persistence/LRUPersistentCache.h>
#include <wangle/client/persistence/SharedMutexCacheLockGuard.h>

using namespace folly;
using namespace std;
using namespace testing;
using namespace wangle;

using TestPersistenceLayer = CachePersistence<string, string>;

using MutexTypes = ::testing::Types<std::mutex, folly::SharedMutex>;
TYPED_TEST_CASE(LRUPersistentCacheTest, MutexTypes);

template<typename T>
static shared_ptr<LRUPersistentCache<string, string, T>> createCache(
    size_t capacity,
    uint32_t syncMillis,
    std::unique_ptr<TestPersistenceLayer> persistence = nullptr) {
  using TestCache = LRUPersistentCache<string, string, T>;
  return std::make_shared<TestCache>(
      capacity, std::chrono::milliseconds(syncMillis), 3, std::move(persistence));
}

class MockPersistenceLayer : public TestPersistenceLayer {
  public:
   ~MockPersistenceLayer() override {
     LOG(ERROR) << "ok.";
    }
    bool persist(const dynamic& obj) noexcept override {
      return persist_(obj);
    }
    Optional<dynamic> load() noexcept override {
      return load_();
    }
    CacheDataVersion getLastPersistedVersionConcrete() const {
      return TestPersistenceLayer::getLastPersistedVersion();
    }
    MOCK_METHOD0(clear, void());
    MOCK_METHOD1(persist_, bool(const dynamic&));
    MOCK_METHOD0(load_, Optional<dynamic>());
    MOCK_CONST_METHOD0(getLastPersistedVersion, CacheDataVersion());
};

template<typename MutexT>
class LRUPersistentCacheTest : public Test {
  protected:
   void SetUp() override {
     persistence = make_unique<MockPersistenceLayer>();
     ON_CALL(*persistence, getLastPersistedVersion())
         .WillByDefault(Invoke(
             persistence.get(),
             &MockPersistenceLayer::getLastPersistedVersionConcrete));
    }

    unique_ptr<MockPersistenceLayer> persistence;
};

TYPED_TEST(LRUPersistentCacheTest, NullPersistence) {
  // make sure things sync even without a persistence layer
  auto cache = createCache<TypeParam>(10, 1, nullptr);
  cache->put("k0", "v0");
  makeFuture().delayed(std::chrono::milliseconds(20))
    .then([cache]{
        auto val = cache->get("k0");
        EXPECT_TRUE(val);
        EXPECT_EQ(*val, "v0");
        EXPECT_FALSE(cache->hasPendingUpdates());
        });
}

MATCHER_P(DynSize, n, "") {
  return size_t(n) == arg.size();
}

TYPED_TEST(LRUPersistentCacheTest, SettingPersistence) {
  auto cache = createCache<TypeParam>(10, 10, nullptr);
  cache->put("k0", "v0");
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  InSequence seq;
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  EXPECT_CALL(*this->persistence, persist_(DynSize(2)))
    .Times(1)
    .WillOnce(Return(true));
  cache->setPersistence(std::move(this->persistence));
}

TYPED_TEST(LRUPersistentCacheTest, SetPersistenceMidPersist) {
  // Setup a cache with no persistence layer
  // Add some items
  // Add a persistence layer, that during persist will call a function
  // to set a new persistence layer on the cache
  // Ensure that the new layer is called with the data
  auto cache = createCache<TypeParam>(10, 10, nullptr);
  cache->put("k0", "v0");
  cache->put("k1", "v1");

  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(dynamic::array()));

  auto func = [cache](const folly::dynamic& /* kv */ ) {
    // The cache persistence that we'll set during a call to persist
    auto p2 = make_unique<MockPersistenceLayer>();
    ON_CALL(*p2, getLastPersistedVersion())
      .WillByDefault(
          Invoke(
            p2.get(),
            &MockPersistenceLayer::getLastPersistedVersionConcrete));
    EXPECT_CALL(*p2, load_())
      .Times(1)
      .WillOnce(Return(dynamic::array()));
    EXPECT_CALL(*p2, persist_(DynSize(2)))
      .Times(1)
      .WillOnce(Return(true));

    cache->setPersistence(std::move(p2));
    return true;
  };
  EXPECT_CALL(*this->persistence, persist_(DynSize(2)))
    .Times(1)
    .WillOnce(Invoke(func));

  cache->setPersistence(std::move(this->persistence));
  makeFuture().delayed(std::chrono::milliseconds(100)).get();
}

TYPED_TEST(LRUPersistentCacheTest, PersistNotCalled) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  EXPECT_CALL(*this->persistence, persist_(_))
    .Times(0)
    .WillOnce(Return(false));
  auto cache = createCache<TypeParam>(10, 10, std::move(this->persistence));
  EXPECT_EQ(cache->size(), 1);
}

TYPED_TEST(LRUPersistentCacheTest, PersistentSetBeforeSyncer) {
  EXPECT_CALL(*this->persistence, getLastPersistedVersion())
    .Times(AtLeast(1))
    .WillRepeatedly(
        Invoke(
          this->persistence.get(),
          &MockPersistenceLayer::getLastPersistedVersionConcrete));
  auto cache = createCache<TypeParam>(10, 10, std::move(this->persistence));
}

TYPED_TEST(LRUPersistentCacheTest, ClearKeepPersist) {
  EXPECT_CALL(*this->persistence, clear()).Times(0);
  auto cache = createCache<TypeParam>(10, 10, std::move(this->persistence));
  cache->clear();
}

TYPED_TEST(LRUPersistentCacheTest, ClearDontKeepPersist) {
  EXPECT_CALL(*this->persistence, clear()).Times(1);
  auto cache = createCache<TypeParam>(10, 10, std::move(this->persistence));
  cache->clear(true);
}

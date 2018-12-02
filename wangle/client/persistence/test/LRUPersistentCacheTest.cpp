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

#include <folly/Memory.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/futures/Future.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
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
    std::unique_ptr<TestPersistenceLayer> persistence = nullptr,
    bool loadPersistenceInline = true) {
  using TestCache = LRUPersistentCache<string, string, T>;
  return std::make_shared<TestCache>(
      capacity,
      std::chrono::milliseconds(syncMillis),
      3,
      std::move(persistence),
      loadPersistenceInline);
}

template <typename T>
static shared_ptr<LRUPersistentCache<string, string, T>>
createCacheWithExecutor(
    std::shared_ptr<folly::Executor> executor,
    std::unique_ptr<TestPersistenceLayer> persistence,
    std::chrono::milliseconds syncInterval,
    int retryLimit,
    bool loadPersistenceInline = true) {
  return std::make_shared<LRUPersistentCache<string, string, T>>(
      std::move(executor),
      10,
      syncInterval,
      retryLimit,
      std::move(persistence),
      loadPersistenceInline);
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
    void setPersistedVersionConcrete(CacheDataVersion version) {
      TestPersistenceLayer::setPersistedVersion(version);
    }
    MOCK_METHOD0(clear, void());
    MOCK_METHOD1(persist_, bool(const dynamic&));
    MOCK_METHOD0(load_, Optional<dynamic>());
    MOCK_CONST_METHOD0(getLastPersistedVersion, CacheDataVersion());
    GMOCK_METHOD1_(, noexcept, , setPersistedVersion, void(CacheDataVersion));
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
     ON_CALL(*persistence, setPersistedVersion(_))
         .WillByDefault(Invoke(
             persistence.get(),
             &MockPersistenceLayer::setPersistedVersionConcrete));
     manualExecutor = std::make_shared<folly::ManualExecutor>();
     inlineExecutor = std::make_shared<folly::InlineExecutor>();
   }

   unique_ptr<MockPersistenceLayer> persistence;
   std::shared_ptr<folly::ManualExecutor> manualExecutor;
   std::shared_ptr<folly::InlineExecutor> inlineExecutor;
};

TYPED_TEST(LRUPersistentCacheTest, NullPersistence) {
  // make sure things sync even without a persistence layer
  auto cache = createCache<TypeParam>(10, 1, nullptr);
  cache->init();
  cache->put("k0", "v0");
  makeFuture().delayed(std::chrono::milliseconds(20)).thenValue(
    [cache](auto&&) {
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
  cache->init();
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

TYPED_TEST(LRUPersistentCacheTest, SyncOnDestroy) {
  auto cache = createCache<TypeParam>(10, 10000, nullptr);
  cache->init();
  auto persistence = this->persistence.get();
  cache->setPersistence(std::move(this->persistence));
  cache->put("k0", "v0");
  EXPECT_CALL(*persistence, persist_(_))
    .Times(AtLeast(1))
    .WillRepeatedly(Return(true));
  cache.reset();
}

TYPED_TEST(LRUPersistentCacheTest, SetPersistenceMidPersist) {
  // Setup a cache with no persistence layer
  // Add some items
  // Add a persistence layer, that during persist will call a function
  // to set a new persistence layer on the cache
  // Ensure that the new layer is called with the data
  auto cache = createCache<TypeParam>(10, 10, nullptr);
  cache->init();
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
  cache->init();
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
  cache->init();
}

TYPED_TEST(LRUPersistentCacheTest, ClearKeepPersist) {
  EXPECT_CALL(*this->persistence, clear()).Times(0);
  auto cache = createCache<TypeParam>(10, 10, std::move(this->persistence));
  cache->init();
  cache->clear();
}

TYPED_TEST(LRUPersistentCacheTest, ClearDontKeepPersist) {
  EXPECT_CALL(*this->persistence, clear()).Times(1);
  auto cache = createCache<TypeParam>(10, 10, std::move(this->persistence));
  cache->init();
  cache->clear(true);
}

TYPED_TEST(LRUPersistentCacheTest, ExecutorCacheDeallocBeforeAdd) {
  auto cache = createCacheWithExecutor<TypeParam>(
      this->manualExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      1);
  cache.reset();
  // Nothing should happen here
  this->manualExecutor->drain();
}

TYPED_TEST(LRUPersistentCacheTest, ExecutorCacheRunTask) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  auto rawPersistence = this->persistence.get();
  auto cache = createCacheWithExecutor<TypeParam>(
      this->manualExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      1);
  cache->init();
  this->manualExecutor->run();
  cache->put("k0", "v0");
  EXPECT_CALL(*rawPersistence, getLastPersistedVersion())
      .Times(1)
      .WillOnce(Invoke(
          rawPersistence,
          &MockPersistenceLayer::getLastPersistedVersionConcrete));
  EXPECT_CALL(*rawPersistence, persist_(DynSize(2)))
      .Times(1)
      .WillOnce(Return(true));
  this->manualExecutor->run();
  cache.reset();
}

TYPED_TEST(LRUPersistentCacheTest, ExecutorCacheRunTaskInline) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  auto rawPersistence = this->persistence.get();
  auto cache = createCacheWithExecutor<TypeParam>(
      this->inlineExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      1);
  cache->init();
  this->manualExecutor->run();
  EXPECT_CALL(*rawPersistence, getLastPersistedVersion())
      .Times(1)
      .WillOnce(Invoke(
          rawPersistence,
          &MockPersistenceLayer::getLastPersistedVersionConcrete));
  EXPECT_CALL(*rawPersistence, persist_(DynSize(2)))
      .Times(1)
      .WillOnce(Return(true));
  cache->put("k0", "v0");

  EXPECT_CALL(*rawPersistence, getLastPersistedVersion())
      .Times(1)
      .WillOnce(Invoke(
          rawPersistence,
          &MockPersistenceLayer::getLastPersistedVersionConcrete));
  EXPECT_CALL(*rawPersistence, persist_(DynSize(3)))
      .Times(1)
      .WillOnce(Return(true));
  cache->put("k2", "v2");
  cache.reset();
}

TYPED_TEST(LRUPersistentCacheTest, ExecutorCacheRetries) {
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(dynamic::array()));
  auto rawPersistence = this->persistence.get();
  auto cache = createCacheWithExecutor<TypeParam>(
      this->manualExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      2);
  cache->init();
  this->manualExecutor->run();
  EXPECT_CALL(*rawPersistence, getLastPersistedVersion())
      .WillRepeatedly(Invoke(
          rawPersistence,
          &MockPersistenceLayer::getLastPersistedVersionConcrete));

  cache->put("k0", "v0");
  EXPECT_CALL(*rawPersistence, persist_(DynSize(1)))
      .Times(1)
      .WillOnce(Return(false));
  this->manualExecutor->run();

  cache->put("k1", "v1");
  EXPECT_CALL(*rawPersistence, persist_(DynSize(2)))
      .Times(1)
      .WillOnce(Return(false));
  // reached retry limit, so we will set a version anyway
  EXPECT_CALL(*rawPersistence, setPersistedVersion(_))
      .Times(1)
      .WillOnce(Invoke(
          rawPersistence, &MockPersistenceLayer::setPersistedVersionConcrete));
  this->manualExecutor->run();

  cache.reset();
}

TYPED_TEST(LRUPersistentCacheTest, ExecutorCacheSchduledAndDealloc) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  auto cache = createCacheWithExecutor<TypeParam>(
      this->manualExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      1);
  cache->init();
  this->manualExecutor->run();
  cache->put("k0", "v0");
  cache->put("k2", "v2");

  // Kill cache first then try to run scheduled tasks. Nothing will run and no
  // one should crash.
  cache.reset();
  this->manualExecutor->drain();
}

TYPED_TEST(LRUPersistentCacheTest, ExecutorCacheScheduleInterval) {
 EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(dynamic::array()));
  auto rawPersistence = this->persistence.get();
  auto cache = createCacheWithExecutor<TypeParam>(
      this->manualExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds(60 * 60 * 1000),
      1);
  cache->init();
  this->manualExecutor->run();
  EXPECT_CALL(*rawPersistence, getLastPersistedVersion())
      .WillRepeatedly(Invoke(
          rawPersistence,
          &MockPersistenceLayer::getLastPersistedVersionConcrete));

  cache->put("k0", "v0");
  EXPECT_CALL(*rawPersistence, persist_(DynSize(1)))
      .Times(1)
      .WillOnce(Return(false));
  this->manualExecutor->run();

  // None of the following will trigger a run
  EXPECT_CALL(*rawPersistence, persist_(DynSize(2))).Times(0);
  EXPECT_CALL(*rawPersistence, setPersistedVersion(_)).Times(0);
  cache->put("k1", "v1");
  this->manualExecutor->run();
  cache.reset();
  this->manualExecutor->drain();
}

TYPED_TEST(LRUPersistentCacheTest, InitCache) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  auto cache = createCacheWithExecutor<TypeParam>(
      this->inlineExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      1,
      false);
  cache->init();
  EXPECT_FALSE(cache->hasPendingUpdates());
}

TYPED_TEST(LRUPersistentCacheTest, BlockingAccessCanContinueWithExecutor) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  auto cache = createCacheWithExecutor<TypeParam>(
      this->manualExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      1,
      false);
  cache->init();
  std::string value1, value2;
  std::thread willBeBlocked([&]() { value1 = cache->get("k1").value(); });
  // Without running the executor to finish setPersistenceHelper in init,
  // willbeBlocked will be blocked.
  this->manualExecutor->run();
  willBeBlocked.join();
  EXPECT_EQ("v1", value1);

  std::thread wontBeBlocked([&]() { value2 = cache->get("k1").value(); });
  wontBeBlocked.join();
  EXPECT_EQ("v1", value2);
}

TYPED_TEST(LRUPersistentCacheTest, BlockingAccessCanContinueWithThread) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  EXPECT_CALL(*this->persistence, load_())
    .Times(1)
    .WillOnce(Return(data));
  auto cache =
      createCache<TypeParam>(10, 10, std::move(this->persistence), false);
  cache->init();
  std::string value1;
  std::thread appThread([&]() { value1 = cache->get("k1").value(); });
  appThread.join();
  EXPECT_EQ("v1", value1);
}

TYPED_TEST(
    LRUPersistentCacheTest,
    PersistenceOnlyLoadedOnceFromCtorWithExecutor) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  auto persistence = this->persistence.get();
  EXPECT_CALL(*persistence, load_()).Times(1).WillOnce(Return(data));
  auto cache = createCacheWithExecutor<TypeParam>(
      this->manualExecutor,
      std::move(this->persistence),
      std::chrono::milliseconds::zero(),
      1);
  EXPECT_CALL(*persistence, load_()).Times(0);
  cache->init();
}

TYPED_TEST(
    LRUPersistentCacheTest,
    PersistenceOnlyLoadedOnceFromCtorWithSyncThread) {
  folly::dynamic data = dynamic::array(dynamic::array("k1", "v1"));
  auto persistence = this->persistence.get();
  EXPECT_CALL(*persistence, load_()).Times(1).WillOnce(Return(data));
  auto cache = createCache<TypeParam>(10, 1, std::move(this->persistence));
  EXPECT_CALL(*persistence, load_()).Times(0);
  cache->init();
}

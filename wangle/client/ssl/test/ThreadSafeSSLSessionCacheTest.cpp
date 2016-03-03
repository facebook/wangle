/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/client/ssl/ThreadSafeSSLSessionCache.h>
#include <wangle/client/ssl/test/TestUtil.h>
#include <folly/Conv.h>
#include <folly/Memory.h>

#include <thread>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace testing;
using namespace wangle;

// One time use cache for testing.
class FakeSessionCallbacks : public SSLSessionCallbacks {
 public:
   void setSSLSession(
       const std::string& identity,
       SSLSessionPtr session) noexcept {
     cache_.emplace(identity, std::move(session));
   }

   SSLSessionPtr getSSLSession(const std::string& identity) const noexcept {
     auto it = cache_.find(identity);
     if (it == cache_.end()) {
       return SSLSessionPtr(nullptr);
     }
     auto sess = std::move(it->second);
     cache_.erase(it);
     return sess;
   }

   bool removeSSLSession(const std::string& identity) noexcept {
     return true;
   }

 private:
   mutable std::map<std::string, SSLSessionPtr> cache_;
};

class ThreadSafeSSLSessionCacheTest : public Test {
 public:
   ThreadSafeSSLSessionCacheTest() {}

   void SetUp() override {
     for (auto& it : getSessions()) {
       sessions_.emplace_back(it.first, it.second);
     }
     cache_.reset(new ThreadSafeSSLSessionCache(
           folly::make_unique<FakeSessionCallbacks>()));
   }

   std::vector<std::pair<SSL_SESSION*, size_t>> sessions_;
   std::unique_ptr<ThreadSafeSSLSessionCache> cache_;
};

TEST_F(ThreadSafeSSLSessionCacheTest, ReadWrite) {
  const size_t numRounds = 100;

  size_t writeOps = 0;
  size_t readOps = 0;

  std::thread writer([&] () {
    for (size_t j = 0; j < numRounds; ++j) {
      for (size_t i = 0; i < sessions_.size(); ++i) {
        writeOps++;
        cache_->setSSLSession(
            folly::to<std::string>("host ", j, i),
            createPersistentTestSession(sessions_[i]));
      }
    }
  });

  std::thread reader([&] () {
    for (size_t j = 0; j < numRounds; ++j) {
      for (size_t i = 0; i < sessions_.size(); ++i) {
        readOps++;
        auto sess = cache_->getSSLSession(
            folly::to<std::string>("host ", j, i));
        if (!sess) {
          // spinlock around the session.
          i--;
        }
      }
    }
  });

  writer.join();
  reader.join();
  EXPECT_GE(readOps, writeOps);
}

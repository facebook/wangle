/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>
#include <wangle/concurrent/GlobalExecutor.h>
#include <wangle/concurrent/IOExecutor.h>

using namespace folly;
using namespace wangle;

TEST(GlobalExecutorTest, GlobalCPUExecutor) {
  class DummyExecutor : public folly::Executor {
   public:
    void add(folly::Func f) override {
      f();
      count++;
    }
    int count{0};
  };

  // The default CPU executor is a synchronous inline executor, lets verify
  // that work we add is executed
  auto count = 0;
  auto f = [&](){ count++; };

  // Don't explode, we should create the default global CPUExecutor lazily here.
  getCPUExecutor()->add(f);
  EXPECT_EQ(1, count);

  {
    auto dummy = std::make_shared<DummyExecutor>();
    setCPUExecutor(dummy);
    getCPUExecutor()->add(f);
    // Make sure we were properly installed.
    EXPECT_EQ(1, dummy->count);
    EXPECT_EQ(2, count);
  }

  // Don't explode, we should restore the default global CPUExecutor because our
  // weak reference to dummy has expired
  getCPUExecutor()->add(f);
  EXPECT_EQ(3, count);
}

TEST(GlobalExecutorTest, GlobalIOExecutor) {
  class DummyExecutor : public IOExecutor {
   public:
    void add(folly::Func f) override {
      count++;
    }
    folly::EventBase* getEventBase() override {
      return nullptr;
    }
    int count{0};
  };

  auto f = [](){};

  // Don't explode, we should create the default global IOExecutor lazily here.
  getIOExecutor()->add(f);

  {
    auto dummy = std::make_shared<DummyExecutor>();
    setIOExecutor(dummy);
    getIOExecutor()->add(f);
    // Make sure we were properly installed.
    EXPECT_EQ(1, dummy->count);
  }

  // Don't explode, we should restore the default global IOExecutor because our
  // weak reference to dummy has expired
  getIOExecutor()->add(f);
}

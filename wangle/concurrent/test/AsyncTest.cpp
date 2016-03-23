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
#include <wangle/concurrent/Async.h>
#include <folly/futures/ManualExecutor.h>

using namespace folly;
using namespace wangle;

TEST(AsyncFunc, manual_executor) {
  auto x = std::make_shared<ManualExecutor>();
  auto oldX = getCPUExecutor();
  setCPUExecutor(x);
  auto f = async([]{ return 42; });
  EXPECT_FALSE(f.isReady());
  x->run();
  EXPECT_EQ(42, f.value());
  setCPUExecutor(oldX);
}

TEST(AsyncFunc, value_lambda) {
  auto lambda = []{ return 42; };
  auto future = async(lambda);
  EXPECT_EQ(42, future.get());
}

TEST(AsyncFunc, void_lambda) {
  auto lambda = []{/*do something*/ return; };
  auto future = async(lambda);
  //Futures with a void returning function, return Unit type
  EXPECT_EQ(typeid(Unit), typeid(future.get()));
}

TEST(AsyncFunc, moveonly_lambda) {
  auto lambda = []{return std::unique_ptr<int>(new int(42)); };
  auto future = async(lambda);
  EXPECT_EQ(42, *future.get() );
}

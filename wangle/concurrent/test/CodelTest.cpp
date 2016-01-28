/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <chrono>
#include <wangle/concurrent/Codel.h>
#include <gtest/gtest.h>
#include <thread>

using std::chrono::milliseconds;
using std::this_thread::sleep_for;

TEST(CodelTest, Basic) {
  wangle::Codel c;
  std::this_thread::sleep_for(milliseconds(110));
  // This interval is overloaded
  EXPECT_FALSE(c.overloaded(milliseconds(100)));
  std::this_thread::sleep_for(milliseconds(90));
  // At least two requests must happen in an interval before they will fail
  EXPECT_FALSE(c.overloaded(milliseconds(50)));
  EXPECT_TRUE(c.overloaded(milliseconds(50)));
  std::this_thread::sleep_for(milliseconds(110));
  // Previous interval is overloaded, but 2ms isn't enough to fail
  EXPECT_FALSE(c.overloaded(milliseconds(2)));
  std::this_thread::sleep_for(milliseconds(90));
  // 20 ms > target interval * 2
  EXPECT_TRUE(c.overloaded(milliseconds(20)));
}

TEST(CodelTest, highLoad) {
  wangle::Codel c;
  c.overloaded(milliseconds(40));
  EXPECT_EQ(100, c.getLoad());
}

TEST(CodelTest, mediumLoad) {
  wangle::Codel c;
  c.overloaded(milliseconds(20));
  sleep_for(milliseconds(90));
  // this is overloaded but this request shouldn't drop because it's not >
  // slough timeout
  EXPECT_FALSE(c.overloaded(milliseconds(8)));
  EXPECT_GT(100, c.getLoad());
}

TEST(CodelTest, reducingLoad) {
  wangle::Codel c;
  c.overloaded(milliseconds(20));
  sleep_for(milliseconds(90));
  EXPECT_FALSE(c.overloaded(milliseconds(4)));
}

TEST(CodelTest, oneRequestNoDrop) {
  wangle::Codel c;
  EXPECT_FALSE(c.overloaded(milliseconds(20)));
}

TEST(CodelTest, getLoadSanity) {
  wangle::Codel c;
  // should be 100% but leave a litte wiggle room.
  c.overloaded(milliseconds(10));
  EXPECT_LT(99, c.getLoad());
  EXPECT_GT(101, c.getLoad());

  // should be 70% but leave a litte wiggle room.
  c.overloaded(milliseconds(7));
  EXPECT_LT(60, c.getLoad());
  EXPECT_GT(80, c.getLoad());

  // should be 20% but leave a litte wiggle room.
  c.overloaded(milliseconds(2));
  EXPECT_LT(10, c.getLoad());
  EXPECT_GT(30, c.getLoad());


  // this test demonstrates how silly getLoad() is, but silly isn't
  // necessarily useless
}

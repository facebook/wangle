/*
 *  Copyright (c) 2015, Facebook, Inc.
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

TEST(CodelTest, Basic) {
  using std::chrono::milliseconds;
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

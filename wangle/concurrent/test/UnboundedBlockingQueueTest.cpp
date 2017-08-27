/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/portability/GTest.h>
#include <folly/Baton.h>
#include <wangle/concurrent/UnboundedBlockingQueue.h>
#include <thread>

using namespace wangle;

TEST(UnboundedQueuee, push_pop) {
  UnboundedBlockingQueue<int> q;
  q.add(42);
  EXPECT_EQ(42, q.take());
}
TEST(UnboundedBlockingQueue, size) {
  UnboundedBlockingQueue<int> q;
  EXPECT_EQ(0, q.size());
  q.add(42);
  EXPECT_EQ(1, q.size());
  q.take();
  EXPECT_EQ(0, q.size());
}

TEST(UnboundedBlockingQueue, concurrent_push_pop) {
  UnboundedBlockingQueue<int> q;
  folly::Baton<> b1, b2;
  std::thread t([&] {
    b1.post();
    EXPECT_EQ(42, q.take());
    EXPECT_EQ(0, q.size());
    b2.post();
  });
  b1.wait();
  q.add(42);
  b2.wait();
  EXPECT_EQ(0, q.size());
  t.join();
}

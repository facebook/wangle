/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <memory>

#include <wangle/concurrent/FiberIOExecutor.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>

#include <folly/portability/GTest.h>

namespace {

class FiberIOExecutorTest : public testing::Test {};

}

TEST_F(FiberIOExecutorTest, event_base) {
  auto tpe = std::make_shared<wangle::IOThreadPoolExecutor>(1);
  wangle::FiberIOExecutor e(tpe);

  ASSERT_NE(e.getEventBase(), nullptr);
  ASSERT_EQ(e.getEventBase(), tpe->getEventBase());
}

TEST_F(FiberIOExecutorTest, basic_execution) {
  auto tpe = std::make_shared<wangle::IOThreadPoolExecutor>(1);
  wangle::FiberIOExecutor e(tpe);

  // FiberIOExecutor should add tasks using the FiberManager mapped to the
  // IOThreadPoolExecutor's event base.
  folly::Baton<> baton;
  bool inContext = false;

  e.add([&](){
    inContext = folly::fibers::onFiber();
    auto& lc = dynamic_cast<folly::fibers::EventBaseLoopController&>(
        folly::fibers::getFiberManager(*e.getEventBase()).loopController());
    auto& eb = lc.getEventBase()->getEventBase();
    inContext =
        inContext && &eb == folly::EventBaseManager::get()->getEventBase();
    baton.post();
  });
  baton.wait();

  ASSERT_TRUE(inContext);
}

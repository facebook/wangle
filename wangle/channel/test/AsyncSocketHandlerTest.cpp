/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/test/MockPipeline.h>

using namespace folly;
using namespace testing;
using namespace wangle;

TEST(AsyncSocketHandlerTest, WriteErrOnShutdown) {
  StrictMock<MockPipelineManager> manager;

  EventBase evb;
  auto socket = AsyncSocket::newSocket(&evb);
  auto pipeline = DefaultPipeline::create();
  pipeline->setPipelineManager(&manager);
  pipeline->addBack(AsyncSocketHandler(socket)).finalize();

  // close() the pipeline multiple times.
  // deletePipeline should only be called once.
  EXPECT_CALL(manager, deletePipeline(_)).Times(1);
  pipeline->close();
  pipeline->close();
}

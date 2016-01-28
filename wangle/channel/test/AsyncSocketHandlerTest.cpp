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
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/test/MockHandler.h>
#include <wangle/channel/test/MockPipeline.h>

using namespace folly;
using namespace testing;
using namespace wangle;

TEST(AsyncSocketHandlerTest, WriteErrOnShutdown) {
  InSequence dummy;

  EventBase evb;
  auto socket = AsyncSocket::newSocket(&evb);
  StrictMock<MockPipelineManager> manager;
  auto pipeline = DefaultPipeline::create();
  pipeline->setPipelineManager(&manager);
  pipeline->addBack(AsyncSocketHandler(socket)).finalize();

  // close() the pipeline multiple times.
  // deletePipeline should only be called once.
  EXPECT_CALL(manager, deletePipeline(_)).Times(1);
  pipeline->close();
  pipeline->close();
}

TEST(AsyncSocketHandlerTest, TransportActiveInactive) {
  InSequence dummy;

  EventBase evb;
  auto socket = AsyncSocket::newSocket(&evb);
  auto handler = std::make_shared<StrictMock<MockBytesToBytesHandler>>();
  auto pipeline = DefaultPipeline::create();
  pipeline->addBack(AsyncSocketHandler(socket));
  pipeline->addBack(handler);
  pipeline->finalize();

  EXPECT_CALL(*handler, transportActive(_)).Times(1);
  pipeline->transportActive();
  EXPECT_CALL(*handler, transportInactive(_)).Times(1);
  pipeline->transportInactive();
  EXPECT_CALL(*handler, transportActive(_)).Times(1);
  pipeline->transportActive();
  // Transport is currently active. Calling pipeline->close()
  // should result in transportInactive being fired.
  EXPECT_CALL(*handler, mockClose(_))
    .WillOnce(Return(handler->defaultFuture()));
  EXPECT_CALL(*handler, transportInactive(_)).Times(1);
  pipeline->close();

  socket = AsyncSocket::newSocket(&evb);
  handler = std::make_shared<StrictMock<MockBytesToBytesHandler>>();
  pipeline = DefaultPipeline::create();
  pipeline->addBack(AsyncSocketHandler(socket));
  pipeline->addBack(handler);
  pipeline->finalize();

  EXPECT_CALL(*handler, transportActive(_)).Times(1);
  pipeline->transportActive();
  EXPECT_CALL(*handler, transportInactive(_)).Times(1);
  pipeline->transportInactive();
  // Transport is currently inactive. Calling pipeline->close()
  // should not result in transportInactive being fired.
  EXPECT_CALL(*handler, mockClose(_))
    .WillOnce(Return(handler->defaultFuture()));
  EXPECT_CALL(*handler, transportInactive(_)).Times(0);
  pipeline->close();
}

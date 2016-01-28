/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/channel/StaticPipeline.h>
#include <wangle/channel/OutputBufferingHandler.h>
#include <wangle/channel/test/MockHandler.h>
#include <folly/io/async/AsyncSocket.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace wangle;
using namespace testing;

typedef StrictMock<MockHandlerAdapter<
  IOBufQueue&,
  std::unique_ptr<IOBuf>>>
MockBytesHandler;

MATCHER_P(IOBufContains, str, "") { return arg->moveToFbString() == str; }

TEST(OutputBufferingHandlerTest, Basic) {
  MockBytesHandler mockHandler;
  EXPECT_CALL(mockHandler, attachPipeline(_));
  auto pipeline = StaticPipeline<IOBufQueue&, std::unique_ptr<IOBuf>,
    MockBytesHandler,
    OutputBufferingHandler>::create(
      &mockHandler,
      OutputBufferingHandler());

  EventBase eb;
  auto socket = AsyncSocket::newSocket(&eb);
  pipeline->setTransport(socket);

  // Buffering should prevent writes until the EB loops, and the writes should
  // be batched into one write call.
  auto f1 = pipeline->write(IOBuf::copyBuffer("hello"));
  auto f2 = pipeline->write(IOBuf::copyBuffer("world"));
  EXPECT_FALSE(f1.isReady());
  EXPECT_FALSE(f2.isReady());
  EXPECT_CALL(mockHandler, write_(_, IOBufContains("helloworld")));
  eb.loopOnce();
  EXPECT_TRUE(f1.isReady());
  EXPECT_TRUE(f2.isReady());
  EXPECT_CALL(mockHandler, detachPipeline(_));

 // Make sure the SharedPromise resets correctly
  auto f = pipeline->write(IOBuf::copyBuffer("foo"));
  EXPECT_FALSE(f.isReady());
  EXPECT_CALL(mockHandler, write_(_, IOBufContains("foo")));
  eb.loopOnce();
  EXPECT_TRUE(f.isReady());
}

/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/channel/FileRegion.h>
#include <folly/io/async/test/AsyncSocketTest.h>
#include <gtest/gtest.h>

#ifdef SPLICE_F_NONBLOCK
using namespace folly;
using namespace wangle;
using namespace testing;

struct FileRegionTest : public Test {
  FileRegionTest() {
    // Connect
    socket = AsyncSocket::newSocket(&evb);
    socket->connect(&ccb, server.getAddress(), 30);

    // Accept the connection
    acceptedSocket = server.acceptAsync(&evb);
    acceptedSocket->setReadCB(&rcb);

    // Create temp file
    char path[] = "/tmp/AsyncSocketTest.WriteFile.XXXXXX";
    fd = mkostemp(path, O_RDWR);
    EXPECT_TRUE(fd > 0);
    EXPECT_EQ(0, unlink(path));
  }

  ~FileRegionTest() override {
    // Close up shop
    close(fd);
    acceptedSocket->close();
    socket->close();
  }

  TestServer server;
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket;
  std::shared_ptr<AsyncSocket> acceptedSocket;
  ConnCallback ccb;
  ReadCallback rcb;
  int fd;
};

TEST_F(FileRegionTest, Basic) {
  size_t count = 1000000000; // 1 GB
  void* zeroBuf = calloc(1, count);
  write(fd, zeroBuf, count);

  FileRegion fileRegion(fd, 0, count);
  auto f = fileRegion.transferTo(socket);
  try {
    f.getVia(&evb);
  } catch (std::exception& e) {
    LOG(FATAL) << exceptionStr(e);
  }

  // Let the reads run to completion
  socket->shutdownWrite();
  evb.loop();

  ASSERT_EQ(rcb.state, STATE_SUCCEEDED);

  size_t receivedBytes = 0;
  for (auto& buf : rcb.buffers) {
    receivedBytes += buf.length;
    ASSERT_EQ(memcmp(buf.buffer, zeroBuf, buf.length), 0);
  }
  ASSERT_EQ(receivedBytes, count);
}

TEST_F(FileRegionTest, Repeated) {
  size_t count = 1000000;
  void* zeroBuf = calloc(1, count);
  write(fd, zeroBuf, count);

  int sendCount = 1000;

  FileRegion fileRegion(fd, 0, count);
  std::vector<Future<Unit>> fs;
  for (int i = 0; i < sendCount; i++) {
    fs.push_back(fileRegion.transferTo(socket));
  }
  auto f = collect(fs);
  ASSERT_NO_THROW(f.getVia(&evb));

  // Let the reads run to completion
  socket->shutdownWrite();
  evb.loop();

  ASSERT_EQ(rcb.state, STATE_SUCCEEDED);

  size_t receivedBytes = 0;
  for (auto& buf : rcb.buffers) {
    receivedBytes += buf.length;
  }
  ASSERT_EQ(receivedBytes, sendCount*count);
}
#endif

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

#include <wangle/codec/FixedLengthFrameDecoder.h>
#include <wangle/codec/LengthFieldBasedFrameDecoder.h>
#include <wangle/codec/LengthFieldPrepender.h>
#include <wangle/codec/LineBasedFrameDecoder.h>

using namespace folly;
using namespace wangle;
using namespace folly::io;

class FrameTester
    : public InboundHandler<std::unique_ptr<IOBuf>> {
 public:
  explicit FrameTester(std::function<void(std::unique_ptr<IOBuf>)> test)
    : test_(test) {}

  void read(Context* ctx, std::unique_ptr<IOBuf> buf) override {
    test_(std::move(buf));
  }

  void readException(Context* ctx, exception_wrapper w) override {
    test_(nullptr);
  }
 private:
  std::function<void(std::unique_ptr<IOBuf>)> test_;
};

class BytesReflector
    : public BytesToBytesHandler {
 public:
  Future<Unit> write(Context* ctx, std::unique_ptr<IOBuf> buf) override {
    IOBufQueue q_(IOBufQueue::cacheChainLength());
    q_.append(std::move(buf));
    ctx->fireRead(q_);

    return makeFuture();
  }
};

TEST(FixedLengthFrameDecoder, FailWhenLengthFieldEndOffset) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(FixedLengthFrameDecoder(10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 10);
      }))
    .finalize();

  auto buf3 = IOBuf::create(3);
  buf3->append(3);
  auto buf11 = IOBuf::create(11);
  buf11->append(11);
  auto buf16 = IOBuf::create(16);
  buf16->append(16);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf3));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(buf11));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  q.append(std::move(buf16));
  pipeline->read(q);
  EXPECT_EQ(called, 3);
}

TEST(LengthFieldFramePipeline, SimpleTest) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(BytesReflector())
    .addBack(LengthFieldPrepender())
    .addBack(LengthFieldBasedFrameDecoder())
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 2);
      }))
    .finalize();

  auto buf = IOBuf::create(2);
  buf->append(2);
  pipeline->write(std::move(buf));
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFramePipeline, LittleEndian) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(BytesReflector())
    .addBack(LengthFieldBasedFrameDecoder(4, 100, 0, 0, 4, false))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .addBack(LengthFieldPrepender(4, 0, false, false))
    .finalize();

  auto buf = IOBuf::create(1);
  buf->append(1);
  pipeline->write(std::move(buf));
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, Simple) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder())
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(4);
  bufFrame->append(4);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)1);
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, NoStrip) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 0, 0, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(2);
  bufFrame->append(2);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint16_t)1);
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, Adjustment) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 0, -2, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(2);
  bufFrame->append(2);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint16_t)3); // includes frame size
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, PreHeader) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 2, 0, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 5);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(4);
  bufFrame->append(4);
  RWPrivateCursor c(bufFrame.get());
  c.write((uint16_t)100); // header
  c.writeBE((uint16_t)1); // frame size
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, PostHeader) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 0, 2, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 5);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(4);
  bufFrame->append(4);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint16_t)1); // frame size
  c.write((uint16_t)100); // header
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoderStrip, PrePostHeader) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 2, 2, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(6);
  bufFrame->append(6);
  RWPrivateCursor c(bufFrame.get());
  c.write((uint16_t)100); // pre header
  c.writeBE((uint16_t)1); // frame size
  c.write((uint16_t)100); // post header
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, StripPrePostHeaderFrameInclHeader) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 2, -2, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(6);
  bufFrame->append(6);
  RWPrivateCursor c(bufFrame.get());
  c.write((uint16_t)100); // pre header
  c.writeBE((uint16_t)5); // frame size
  c.write((uint16_t)100); // post header
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, FailTestLengthFieldEndOffset) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(4, 10, 4, -2, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto bufFrame = IOBuf::create(8);
  bufFrame->append(8);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)0); // frame size
  c.write((uint32_t)0); // crap

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, FailTestLengthFieldFrameSize) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(4, 10, 0, 0, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto bufFrame = IOBuf::create(16);
  bufFrame->append(16);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)12); // frame size
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, FailTestLengthFieldInitialBytes) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LengthFieldBasedFrameDecoder(4, 10, 0, 0, 10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto bufFrame = IOBuf::create(16);
  bufFrame->append(16);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)4); // frame size
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LineBasedFrameDecoder, Simple) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LineBasedFrameDecoder(10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto buf = IOBuf::create(3);
  buf->append(3);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c(buf.get());
  c.write<char>('\n');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(4);
  buf->append(4);
  RWPrivateCursor c1(buf.get());
  c1.write(' ');
  c1.write(' ');
  c1.write(' ');

  c1.write('\r');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c2(buf.get());
  c2.write('\n');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 2);
}

TEST(LineBasedFrameDecoder, SaveDelimiter) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LineBasedFrameDecoder(10, false))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 4);
      }))
    .finalize();

  auto buf = IOBuf::create(3);
  buf->append(3);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 0);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c(buf.get());
  c.write<char>('\n');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(3);
  buf->append(3);
  RWPrivateCursor c1(buf.get());
  c1.write(' ');
  c1.write(' ');
  c1.write('\r');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c2(buf.get());
  c2.write('\n');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 2);
}

TEST(LineBasedFrameDecoder, Fail) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LineBasedFrameDecoder(10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto buf = IOBuf::create(11);
  buf->append(11);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(1);
  buf->append(1);
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(2);
  buf->append(2);
  RWPrivateCursor c(buf.get());
  c.write(' ');
  c.write<char>('\n');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(12);
  buf->append(12);
  RWPrivateCursor c2(buf.get());
  for (int i = 0; i < 11; i++) {
    c2.write(' ');
  }
  c2.write<char>('\n');
  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 2);
}

TEST(LineBasedFrameDecoder, NewLineOnly) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LineBasedFrameDecoder(
               10, true, LineBasedFrameDecoder::TerminatorType::NEWLINE))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .finalize();

  auto buf = IOBuf::create(2);
  buf->append(2);
  RWPrivateCursor c(buf.get());
  c.write<char>('\r');
  c.write<char>('\n');

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

TEST(LineBasedFrameDecoder, CarriageNewLineOnly) {
  auto pipeline = Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  int called = 0;

  (*pipeline)
    .addBack(LineBasedFrameDecoder(
              10, true, LineBasedFrameDecoder::TerminatorType::CARRIAGENEWLINE))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .finalize();

  auto buf = IOBuf::create(3);
  buf->append(3);
  RWPrivateCursor c(buf.get());
  c.write<char>('\n');
  c.write<char>('\r');
  c.write<char>('\n');

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline->read(q);
  EXPECT_EQ(called, 1);
}

/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <gmock/gmock.h>
#include <wangle/channel/Handler.h>

namespace wangle {

template <class Rin, class Rout = Rin, class Win = Rout, class Wout = Rin>
class MockHandler : public Handler<Rin, Rout, Win, Wout> {
 public:
  typedef typename Handler<Rin, Rout, Win, Wout>::Context Context;

  MockHandler() = default;
  MockHandler(MockHandler&&) = default;

  MOCK_METHOD2_T(read_, void(Context*, Rin&));
  MOCK_METHOD1_T(readEOF, void(Context*));
  MOCK_METHOD2_T(readException, void(Context*, folly::exception_wrapper));

  MOCK_METHOD2_T(write_, void(Context*, Win&));
  MOCK_METHOD1_T(close_, void(Context*));
  MOCK_METHOD2_T(writeException_, void(Context*, folly::exception_wrapper));

  MOCK_METHOD1_T(attachPipeline, void(Context*));
  MOCK_METHOD1_T(attachTransport, void(Context*));
  MOCK_METHOD1_T(detachPipeline, void(Context*));
  MOCK_METHOD1_T(detachTransport, void(Context*));

  void read(Context* ctx, Rin msg) override {
    read_(ctx, msg);
  }

  folly::Future<folly::Unit> write(Context* ctx, Win msg) override {
    return folly::makeFutureWith([&](){
      write_(ctx, msg);
    });
  }

  folly::Future<folly::Unit> close(Context* ctx) override {
    return folly::makeFutureWith([&](){
      close_(ctx);
    });
  }

  folly::Future<folly::Unit> writeException(
    Context* ctx, folly::exception_wrapper ex) override {
    return folly::makeFutureWith([&](){
        writeException_(ctx, ex);
    });
  }
};

template <class R, class W = R>
using MockHandlerAdapter = MockHandler<R, R, W, W>;

class MockBytesToBytesHandler : public BytesToBytesHandler {
 public:
  MOCK_METHOD1(transportActive, void(Context*));
  MOCK_METHOD1(transportInactive, void(Context*));
  MOCK_METHOD2(read, void(Context*, folly::IOBufQueue&));
  MOCK_METHOD1(readEOF, void(Context*));
  MOCK_METHOD2(readException, void(Context*, folly::exception_wrapper));
  MOCK_METHOD2(write,
               folly::Future<folly::Unit>(Context*,
                                          std::shared_ptr<folly::IOBuf>));
  MOCK_METHOD1(close, folly::Future<folly::Unit>(Context*));

  folly::Future<folly::Unit> write(Context* ctx,
                                   std::unique_ptr<folly::IOBuf> buf) override {
    std::shared_ptr<folly::IOBuf> sbuf(buf.release());
    return write(ctx, sbuf);
  }
};

} // namespace wangle

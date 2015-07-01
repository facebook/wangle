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

#include <wangle/channel/Handler.h>
#include <gmock/gmock.h>

namespace folly { namespace wangle {

template <class Rin, class Rout = Rin, class Win = Rout, class Wout = Rin>
class MockHandler : public Handler<Rin, Rout, Win, Wout> {
 public:
  typedef typename Handler<Rin, Rout, Win, Wout>::Context Context;

  MockHandler() = default;
  MockHandler(MockHandler&&) = default;

#ifdef __clang__
# pragma clang diagnostic push
# if __clang_major__ > 3 || __clang_minor__ >= 6
#  pragma clang diagnostic ignored "-Winconsistent-missing-override"
# endif
#endif

  MOCK_METHOD2_T(read_, void(Context*, Rin&));
  MOCK_METHOD1_T(readEOF, void(Context*));
  MOCK_METHOD2_T(readException, void(Context*, exception_wrapper));

  MOCK_METHOD2_T(write_, void(Context*, Win&));
  MOCK_METHOD1_T(close_, void(Context*));

  MOCK_METHOD1_T(attachPipeline, void(Context*));
  MOCK_METHOD1_T(attachTransport, void(Context*));
  MOCK_METHOD1_T(detachPipeline, void(Context*));
  MOCK_METHOD1_T(detachTransport, void(Context*));

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  void read(Context* ctx, Rin msg) override {
    read_(ctx, msg);
  }

  Future<Unit> write(Context* ctx, Win msg) override {
    return makeFutureWith([&](){
      write_(ctx, msg);
    });
  }

  Future<Unit> close(Context* ctx) override {
    return makeFutureWith([&](){
      close_(ctx);
    });
  }
};

template <class R, class W = R>
using MockHandlerAdapter = MockHandler<R, R, W, W>;

}}

/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <folly/futures/Future.h>
#include <wangle/channel/Pipeline.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>

namespace wangle {

template <class Context>
class HandlerBase {
 public:
  virtual ~HandlerBase() = default;

  virtual void attachPipeline(Context* /*ctx*/) {}
  virtual void detachPipeline(Context* /*ctx*/) {}

  Context* getContext() {
    if (attachCount_ != 1) {
      return nullptr;
    }
    CHECK(ctx_);
    return ctx_;
  }

 private:
  friend PipelineContext;
  uint64_t attachCount_{0};
  Context* ctx_{nullptr};
};

template <class Rin, class Rout = Rin, class Win = Rout, class Wout = Rin>
class Handler : public HandlerBase<HandlerContext<Rout, Wout>> {
 public:
  static const HandlerDir dir = HandlerDir::BOTH;

  typedef Rin rin;
  typedef Rout rout;
  typedef Win win;
  typedef Wout wout;
  typedef HandlerContext<Rout, Wout> Context;
  virtual ~Handler() = default;

  virtual void read(Context* ctx, Rin msg) = 0;
  virtual void readEOF(Context* ctx) {
    ctx->fireReadEOF();
  }
  virtual void readException(Context* ctx, folly::exception_wrapper e) {
    ctx->fireReadException(std::move(e));
  }
  virtual void transportActive(Context* ctx) {
    ctx->fireTransportActive();
  }
  virtual void transportInactive(Context* ctx) {
    ctx->fireTransportInactive();
  }

  virtual folly::Future<folly::Unit> write(Context* ctx, Win msg) = 0;
  virtual folly::Future<folly::Unit> writeException(Context* ctx,
                                                   folly::exception_wrapper e) {
    return ctx->fireWriteException(std::move(e));
  }
  virtual folly::Future<folly::Unit> close(Context* ctx) {
    return ctx->fireClose();
  }

  /*
  // Other sorts of things we might want, all shamelessly stolen from Netty
  // inbound
  virtual void exceptionCaught(
      HandlerContext* ctx,
      folly::exception_wrapper e) {}
  virtual void channelRegistered(HandlerContext* ctx) {}
  virtual void channelUnregistered(HandlerContext* ctx) {}
  virtual void channelReadComplete(HandlerContext* ctx) {}
  virtual void userEventTriggered(HandlerContext* ctx, void* evt) {}
  virtual void channelWritabilityChanged(HandlerContext* ctx) {}

  // outbound
  virtual folly::Future<folly::Unit> bind(
      HandlerContext* ctx,
      SocketAddress localAddress) {}
  virtual folly::Future<folly::Unit> connect(
          HandlerContext* ctx,
          SocketAddress remoteAddress, SocketAddress localAddress) {}
  virtual folly::Future<folly::Unit> disconnect(HandlerContext* ctx) {}
  virtual folly::Future<folly::Unit> deregister(HandlerContext* ctx) {}
  virtual folly::Future<folly::Unit> read(HandlerContext* ctx) {}
  virtual void flush(HandlerContext* ctx) {}
  */
};

template <class Rin, class Rout = Rin>
class InboundHandler : public HandlerBase<InboundHandlerContext<Rout>> {
 public:
  static const HandlerDir dir = HandlerDir::IN;

  typedef Rin rin;
  typedef Rout rout;
  typedef folly::Unit win;
  typedef folly::Unit wout;
  typedef InboundHandlerContext<Rout> Context;
  virtual ~InboundHandler() = default;

  virtual void read(Context* ctx, Rin msg) = 0;
  virtual void readEOF(Context* ctx) {
    ctx->fireReadEOF();
  }
  virtual void readException(Context* ctx, folly::exception_wrapper e) {
    ctx->fireReadException(std::move(e));
  }
  virtual void transportActive(Context* ctx) {
    ctx->fireTransportActive();
  }
  virtual void transportInactive(Context* ctx) {
    ctx->fireTransportInactive();
  }
};

template <class Win, class Wout = Win>
class OutboundHandler : public HandlerBase<OutboundHandlerContext<Wout>> {
 public:
  static const HandlerDir dir = HandlerDir::OUT;

  typedef folly::Unit rin;
  typedef folly::Unit rout;
  typedef Win win;
  typedef Wout wout;
  typedef OutboundHandlerContext<Wout> Context;
  virtual ~OutboundHandler() = default;

  virtual folly::Future<folly::Unit> write(Context* ctx, Win msg) = 0;
  virtual folly::Future<folly::Unit> writeException(
      Context* ctx, folly::exception_wrapper e) {
    return ctx->fireWriteException(std::move(e));
  }
  virtual folly::Future<folly::Unit> close(Context* ctx) {
    return ctx->fireClose();
  }
};

template <class R, class W = R>
class HandlerAdapter : public Handler<R, R, W, W> {
 public:
  typedef typename Handler<R, R, W, W>::Context Context;

  void read(Context* ctx, R msg) override {
    ctx->fireRead(std::forward<R>(msg));
  }

  folly::Future<folly::Unit> write(Context* ctx, W msg) override {
    return ctx->fireWrite(std::forward<W>(msg));
  }
};

typedef HandlerAdapter<folly::IOBufQueue&, std::unique_ptr<folly::IOBuf>>
BytesToBytesHandler;

typedef InboundHandler<folly::IOBufQueue&, std::unique_ptr<folly::IOBuf>>
InboundBytesToBytesHandler;

typedef OutboundHandler<std::unique_ptr<folly::IOBuf>>
OutboundBytesToBytesHandler;

} // namespace wangle

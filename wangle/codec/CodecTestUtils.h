/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <memory>

#include <folly/Function.h>
#include <folly/io/IOBuf.h>
#include <wangle/channel/Handler.h>

namespace wangle {
namespace test {

class FrameTester
    : public wangle::InboundHandler<std::unique_ptr<folly::IOBuf>> {
 public:
  explicit FrameTester(
      folly::Function<void(std::unique_ptr<folly::IOBuf>)> test)
      : test_(std::move(test)) {}

  void read(Context*, std::unique_ptr<folly::IOBuf> buf) override {
    test_(std::move(buf));
  }

  void readException(Context*, folly::exception_wrapper) override {
    test_(nullptr);
  }

 private:
  folly::Function<void(std::unique_ptr<folly::IOBuf>)> test_;
};

class BytesReflector : public wangle::BytesToBytesHandler {
 public:
  folly::Future<folly::Unit> write(
      Context* ctx,
      std::unique_ptr<folly::IOBuf> buf) override {
    folly::IOBufQueue q_(folly::IOBufQueue::cacheChainLength());
    q_.append(std::move(buf));
    ctx->fireRead(q_);

    return folly::makeFuture();
  }
};
}
}

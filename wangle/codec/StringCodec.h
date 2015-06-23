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

namespace folly { namespace wangle {

/*
 * StringCodec converts a pipeline from IOBufs to std::strings.
 */
class StringCodec : public Handler<std::unique_ptr<IOBuf>, std::string,
                                   std::string, std::unique_ptr<IOBuf>> {
 public:
  typedef typename Handler<
   std::unique_ptr<IOBuf>, std::string,
   std::string, std::unique_ptr<IOBuf>>::Context Context;

  void read(Context* ctx, std::unique_ptr<IOBuf> buf) override {
    buf->coalesce();
    std::string data((const char*)buf->data(), buf->length());

    ctx->fireRead(data);
  }

  Future<void> write(Context* ctx, std::string msg) override {
    auto buf = IOBuf::copyBuffer(msg.data(), msg.length());
    return ctx->fireWrite(std::move(buf));
  }
};

}} // namespace

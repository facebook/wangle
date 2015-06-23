/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/codec/ByteToMessageCodec.h>

namespace folly { namespace wangle {

void ByteToMessageCodec::read(Context* ctx, IOBufQueue& q) {
  size_t needed = 0;
  std::unique_ptr<IOBuf> result;
  while (true) {
    result = decode(ctx, q, needed);
    if (result) {
      ctx->fireRead(std::move(result));
    } else {
      break;
    }
  }
}

}} // namespace

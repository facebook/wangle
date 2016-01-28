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

#include <wangle/channel/Handler.h>

namespace wangle {

/**
 * A Handler which decodes bytes in a stream-like fashion from
 * IOBufQueue to a  Message type.
 *
 * Frame detection
 *
 * Generally frame detection should be handled earlier in the pipeline
 * by adding a DelimiterBasedFrameDecoder, FixedLengthFrameDecoder,
 * LengthFieldBasedFrameDecoder, LineBasedFrameDecoder.
 *
 * If a custom frame decoder is required, then one needs to be careful
 * when implementing one with {@link ByteToMessageDecoder}. Ensure
 * there are enough bytes in the buffer for a complete frame by
 * checking {@link ByteBuf#readableBytes()}. If there are not enough
 * bytes for a complete frame, return without modify the reader index
 * to allow more bytes to arrive.
 *
 * To check for complete frames without modify the reader index, use
 * IOBufQueue.front(), without split() or pop_front().
 */
template <typename M>
class ByteToMessageDecoder : public InboundHandler<folly::IOBufQueue&, M> {
 public:
  typedef typename InboundHandler<folly::IOBufQueue&, M>::Context Context;

  /**
   * Decode bytes from buf into result.
   *
   * @return bool - Return true if decoding is successful, false if buf
   *                has insufficient bytes.
   */
  virtual bool decode(Context* ctx, folly::IOBufQueue& buf, M& result, size_t&) = 0;

  void read(Context* ctx, folly::IOBufQueue& q) override {
    bool success = true;
    do {
      M result;
      size_t needed = 0;
      success = decode(ctx, q, result, needed);
      if (success) {
        ctx->fireRead(std::move(result));
      }
    } while (success);
  }
};

typedef ByteToMessageDecoder<std::unique_ptr<folly::IOBuf>> ByteToByteDecoder;

} // namespace wangle

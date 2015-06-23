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
class ByteToMessageCodec
    : public InboundBytesToBytesHandler {
 public:

  virtual std::unique_ptr<IOBuf> decode(
    Context* ctx, IOBufQueue& buf, size_t&) = 0;

  void read(Context* ctx, IOBufQueue& q);
};

}}

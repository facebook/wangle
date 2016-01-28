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

#include <wangle/codec/ByteToMessageDecoder.h>

namespace wangle {

/**
 * A decoder that splits the received IOBufs by the fixed number
 * of bytes. For example, if you received the following four
 * fragmented packets:
 *
 * +---+----+------+----+
 * | A | BC | DEFG | HI |
 * +---+----+------+----+
 *
 * A FixedLengthFrameDecoder will decode them into the following three
 * packets with the fixed length:
 *
 * +-----+-----+-----+
 * | ABC | DEF | GHI |
 * +-----+-----+-----+
 *
 */
class FixedLengthFrameDecoder : public ByteToByteDecoder {
 public:
  explicit FixedLengthFrameDecoder(size_t length) : length_(length) {}

  bool decode(Context* ctx,
              folly::IOBufQueue& q,
              std::unique_ptr<folly::IOBuf>& result,
              size_t& needed) override {
    if (q.chainLength() < length_) {
      needed = length_ - q.chainLength();
      return false;
    }

    result = q.split(length_);
    return true;
  }

 private:
  size_t length_;
};

} // namespace wangle

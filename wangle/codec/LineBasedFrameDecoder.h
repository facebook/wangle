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

#include <folly/io/Cursor.h>
#include <wangle/codec/ByteToMessageDecoder.h>

namespace wangle {

/**
 * A decoder that splits the received IOBufQueue on line endings.
 *
 * Both "\n" and "\r\n" are handled, or optionally reqire only
 * one or the other.
 */
class LineBasedFrameDecoder : public ByteToByteDecoder {
 public:
  enum class TerminatorType {
    BOTH,
    NEWLINE,
    CARRIAGENEWLINE
  };

  explicit LineBasedFrameDecoder(
      uint32_t maxLength = UINT_MAX,
      bool stripDelimiter = true,
      TerminatorType terminatorType = TerminatorType::BOTH);

  bool decode(Context* ctx,
              folly::IOBufQueue& buf,
              std::unique_ptr<folly::IOBuf>& result,
              size_t&) override;

 private:

  int64_t findEndOfLine(folly::IOBufQueue& buf);

  void fail(Context* ctx, std::string len);

  uint32_t maxLength_;
  bool stripDelimiter_;

  bool discarding_{false};
  uint32_t discardedBytes_{0};

  TerminatorType terminatorType_;
};

} // namespace wangle

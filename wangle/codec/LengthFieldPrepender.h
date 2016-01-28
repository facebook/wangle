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
#include <wangle/channel/Handler.h>

namespace wangle {

/**
 * An encoder that prepends the length of the message.  The length value is
 * prepended as a binary form.
 *
 * For example, LengthFieldPrepender(2)will encode the
 * following 12-bytes string:
 *
 * +----------------+
 * | "HELLO, WORLD" |
 * +----------------+
 *
 * into the following:
 *
 * +--------+----------------+
 * + 0x000C | "HELLO, WORLD" |
 * +--------+----------------+
 *
 * If you turned on the lengthIncludesLengthFieldLength flag in the
 * constructor, the encoded data would look like the following
 * (12 (original data) + 2 (prepended data) = 14 (0xE)):
 *
 * +--------+----------------+
 * + 0x000E | "HELLO, WORLD" |
 * +--------+----------------+
 *
 */
class LengthFieldPrepender : public OutboundBytesToBytesHandler {
 public:
  explicit LengthFieldPrepender(int lengthFieldLength = 4,
                                int lengthAdjustment = 0,
                                bool lengthIncludesLengthField = false,
                                bool networkByteOrder = true);

  folly::Future<folly::Unit> write(
      Context* ctx,
      std::unique_ptr<folly::IOBuf> buf);

 private:
  int lengthFieldLength_;
  int lengthAdjustment_;
  bool lengthIncludesLengthField_;
  bool networkByteOrder_;
};

} // namespace wangle

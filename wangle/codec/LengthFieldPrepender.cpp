/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/codec/LengthFieldPrepender.h>

using folly::Future;
using folly::Unit;
using folly::IOBuf;

namespace wangle {

LengthFieldPrepender::LengthFieldPrepender(
    int lengthFieldLength,
    int lengthAdjustment,
    bool lengthIncludesLengthField,
    bool networkByteOrder)
    : lengthFieldLength_(lengthFieldLength)
    , lengthAdjustment_(lengthAdjustment)
    , lengthIncludesLengthField_(lengthIncludesLengthField)
    , networkByteOrder_(networkByteOrder) {
    CHECK(lengthFieldLength == 1 ||
          lengthFieldLength == 2 ||
          lengthFieldLength == 4 ||
          lengthFieldLength == 8 );
  }

Future<Unit> LengthFieldPrepender::write(
    Context* ctx, std::unique_ptr<IOBuf> buf) {
  int length = lengthAdjustment_ + buf->computeChainDataLength();
  if (lengthIncludesLengthField_) {
    length += lengthFieldLength_;
  }

  if (length < 0) {
    throw std::runtime_error("Length field < 0");
  }

  auto len = IOBuf::create(lengthFieldLength_);
  len->append(lengthFieldLength_);
  folly::io::RWPrivateCursor c(len.get());

  switch (lengthFieldLength_) {
    case 1: {
      if (length >= 256) {
        throw std::runtime_error("length does not fit byte");
      }
      if (networkByteOrder_) {
        c.writeBE((uint8_t)length);
      } else {
        c.writeLE((uint8_t)length);
      }
      break;
    }
    case 2: {
      if (length >= 65536) {
        throw std::runtime_error("length does not fit byte");
      }
      if (networkByteOrder_) {
        c.writeBE((uint16_t)length);
      } else {
        c.writeLE((uint16_t)length);
      }
      break;
    }
    case 4: {
      if (networkByteOrder_) {
        c.writeBE((uint32_t)length);
      } else {
        c.writeLE((uint32_t)length);
      }
      break;
    }
    case 8: {
      if (networkByteOrder_) {
        c.writeBE((uint64_t)length);
      } else {
        c.writeLE((uint64_t)length);
      }
      break;
    }
    default: {
      throw std::runtime_error("Invalid lengthFieldLength");
    }
  }

  len->prependChain(std::move(buf));
  return ctx->fireWrite(std::move(len));
}


} // namespace wangle

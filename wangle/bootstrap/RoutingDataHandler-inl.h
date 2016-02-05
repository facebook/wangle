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

namespace wangle {

template <typename R>
RoutingDataHandler<R>::RoutingDataHandler(uint64_t connId, Callback* cob)
    : connId_(connId), cob_(CHECK_NOTNULL(cob)) {}

template <typename R>
void RoutingDataHandler<R>::read(Context* ctx, folly::IOBufQueue& q) {
  RoutingData routingData;
  if (parseRoutingData(q, routingData)) {
    cob_->onRoutingData(connId_, routingData);
  }
}

template <typename R>
void RoutingDataHandler<R>::readEOF(Context* ctx) {
  const auto& ex = folly::make_exception_wrapper<folly::AsyncSocketException>(
      folly::AsyncSocketException::END_OF_FILE,
      "Received EOF before parsing routing data");
  cob_->onError(connId_, ex);
}

template <typename R>
void RoutingDataHandler<R>::readException(Context* ctx, folly::exception_wrapper ex) {
  cob_->onError(connId_, ex);
}

} // namespace wangle

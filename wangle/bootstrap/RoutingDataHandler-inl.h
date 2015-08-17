// Copyright 2004-present Facebook.  All rights reserved.
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
  VLOG(4) << "Received EOF before parsing routing data";
  cob_->onError(connId_);
}

template <typename R>
void RoutingDataHandler<R>::readException(Context* ctx, folly::exception_wrapper ex) {
  VLOG(4) << "Received exception before parsing routing data: " << ex.what();
  cob_->onError(connId_);
}

} // namespace wangle

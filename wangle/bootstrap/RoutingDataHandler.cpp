// Copyright 2004-present Facebook.  All rights reserved.
#include <wangle/bootstrap/RoutingDataHandler.h>

namespace folly { namespace wangle {

RoutingDataHandler::RoutingDataHandler(uint64_t connId, Callback* cob)
    : connId_(connId), cob_(CHECK_NOTNULL(cob)) {}

void RoutingDataHandler::read(Context* ctx, IOBufQueue& q) {
  RoutingData routingData;
  if (parseRoutingData(q, routingData)) {
    cob_->onRoutingData(connId_, routingData);
  }
}

void RoutingDataHandler::readEOF(Context* ctx) {
  VLOG(4) << "Received EOF before parsing routing data";
  cob_->onError(connId_);
}

void RoutingDataHandler::readException(Context* ctx, exception_wrapper ex) {
  VLOG(4) << "Received exception before parsing routing data: " << ex.what();
  cob_->onError(connId_);
}

}} // namespace folly::wangle

/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/acceptor/ManagedConnection.h>

#include <wangle/acceptor/ConnectionManager.h>

namespace wangle {

ManagedConnection::ManagedConnection()
  : connectionManager_(nullptr) {
}

ManagedConnection::~ManagedConnection() {
  if (connectionManager_) {
    connectionManager_->removeConnection(this);
  }
}

void
ManagedConnection::resetTimeout() {
  if (connectionManager_) {
    resetTimeoutTo(connectionManager_->getDefaultTimeout());
  }
}

void
ManagedConnection::resetTimeoutTo(std::chrono::milliseconds timeout) {
  if (connectionManager_) {
    connectionManager_->scheduleTimeout(this, timeout);
  }
}

void
ManagedConnection::scheduleTimeout(
  folly::HHWheelTimer::Callback* callback,
    std::chrono::milliseconds timeout) {
  if (connectionManager_) {
    connectionManager_->scheduleTimeout(callback, timeout);
  }
}

////////////////////// Globals /////////////////////

std::ostream&
operator<<(std::ostream& os, const ManagedConnection& conn) {
  conn.describe(os);
  return os;
}

} // wangle

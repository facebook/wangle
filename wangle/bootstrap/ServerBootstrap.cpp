/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/concurrent/NamedThreadFactory.h>
#include <wangle/channel/Handler.h>
#include <folly/io/async/EventBaseManager.h>

namespace wangle {

void ServerWorkerPool::threadStarted(
  wangle::ThreadPoolExecutor::ThreadHandle* h) {
  auto worker = acceptorFactory_->newAcceptor(exec_->getEventBase(h));
  workers_.insert({h, worker});

  for(auto socket : *sockets_) {
    socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [this, worker, socket](){
        socketFactory_->addAcceptCB(
          socket, worker.get(), worker->getEventBase());
    });
  }
}

void ServerWorkerPool::threadStopped(
  wangle::ThreadPoolExecutor::ThreadHandle* h) {
  auto worker = workers_.find(h);
  CHECK(worker != workers_.end());

  for (auto socket : *sockets_) {
    socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() {
        socketFactory_->removeAcceptCB(
          socket, worker->second.get(), nullptr);
    });
  }

  if (!worker->second->getEventBase()->isInEventBaseThread()) {
    worker->second->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [=]() {
        worker->second->dropAllConnections();
      });
  } else {
    worker->second->dropAllConnections();
  }

  workers_.erase(worker);
}

} // namespace wangle

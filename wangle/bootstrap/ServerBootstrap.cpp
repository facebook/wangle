/*
 *  Copyright (c) 2016, Facebook, Inc.
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
  {
    Mutex::WriteHolder holder(workersMutex_.get());
    workers_->insert({h, worker});
  }

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
  auto worker = [&] {
    Mutex::WriteHolder holder(workersMutex_.get());
    auto workerIt = workers_->find(h);
    CHECK(workerIt != workers_->end());
    auto w = std::move(workerIt->second);
    workers_->erase(workerIt);
    return w;
  }();

  for (auto socket : *sockets_) {
    socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() {
        socketFactory_->removeAcceptCB(
          socket, worker.get(), nullptr);
    });
  }

  worker->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
    [&]() {
      worker->dropAllConnections();
    });
}

} // namespace wangle

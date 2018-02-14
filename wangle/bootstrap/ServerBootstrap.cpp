/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/bootstrap/ServerBootstrap.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <wangle/channel/Handler.h>
#include <folly/io/async/EventBaseManager.h>

namespace wangle {

void ServerWorkerPool::threadStarted(
  folly::ThreadPoolExecutor::ThreadHandle* h) {
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
  folly::ThreadPoolExecutor::ThreadHandle* h) {
  auto worker = [&]() -> std::shared_ptr<Acceptor> {
    Mutex::WriteHolder holder(workersMutex_.get());
    auto workerIt = workers_->find(h);
    if (workerIt == workers_->end()) {
      // The thread handle may not be present in the map if newAcceptor() throws
      // an exception. For example, some acceptors require TLS keys / certs to
      // start and will throw exceptions if those files do not exist.
      return nullptr;
    }
    auto w = std::move(workerIt->second);
    workers_->erase(workerIt);
    return w;
  }();
  if (!worker) {
    return;
  }

  for (auto socket : *sockets_) {
    socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() {
        socketFactory_->removeAcceptCB(
          socket, worker.get(), nullptr);
    });
  }

  auto evb = worker->getEventBase();

  evb->runImmediatelyOrRunInEventBaseThreadAndWait(
    [w = std::move(worker)]() mutable {
      w->dropAllConnections();
      w.reset();
    });
}

} // namespace wangle

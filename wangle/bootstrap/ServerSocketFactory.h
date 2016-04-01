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

#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/AsyncUDPServerSocket.h>
#include <wangle/acceptor/Acceptor.h>

namespace wangle {

class ServerSocketFactory {
 public:
  virtual std::shared_ptr<folly::AsyncSocketBase> newSocket(
      folly::SocketAddress address, int backlog,
      bool reuse, ServerSocketConfig& config) = 0;

  virtual void removeAcceptCB(
      std::shared_ptr<folly::AsyncSocketBase> sock,
      Acceptor *callback,
      folly::EventBase* base) = 0;

  virtual void addAcceptCB(
      std::shared_ptr<folly::AsyncSocketBase> sock,
      Acceptor* callback,
      folly::EventBase* base) = 0 ;

  virtual ~ServerSocketFactory() = default;
};

class AsyncServerSocketFactory : public ServerSocketFactory {
 public:
  std::shared_ptr<folly::AsyncSocketBase> newSocket(
      folly::SocketAddress address, int /*backlog*/, bool reuse,
      ServerSocketConfig& config) override {

    auto* evb = folly::EventBaseManager::get()->getEventBase();
    std::shared_ptr<folly::AsyncServerSocket> socket(
        new folly::AsyncServerSocket(evb),
        ThreadSafeDestructor());
    socket->setReusePortEnabled(reuse);
    socket->bind(address);

    socket->listen(config.acceptBacklog);
    socket->startAccepting();

    return socket;
  }

  void removeAcceptCB(std::shared_ptr<folly::AsyncSocketBase> s,
                      Acceptor *callback, folly::EventBase* base) override {
    auto socket = std::dynamic_pointer_cast<folly::AsyncServerSocket>(s);
    CHECK(socket);
    socket->removeAcceptCallback(callback, base);
  }

  void addAcceptCB(std::shared_ptr<folly::AsyncSocketBase> s,
                   Acceptor* callback, folly::EventBase* base) override {
    auto socket = std::dynamic_pointer_cast<folly::AsyncServerSocket>(s);
    CHECK(socket);
    socket->addAcceptCallback(callback, base);
  }

  class ThreadSafeDestructor {
   public:
    void operator()(folly::AsyncServerSocket* socket) const {
      folly::EventBase* evb = socket->getEventBase();
      if (evb) {
        evb->runImmediatelyOrRunInEventBaseThreadAndWait([socket]() {
          socket->destroy();
        });
      } else {
        socket->destroy();
      }
    }
  };
};

class AsyncUDPServerSocketFactory : public ServerSocketFactory {
 public:
  std::shared_ptr<folly::AsyncSocketBase> newSocket(
      folly::SocketAddress address, int /*backlog*/, bool reuse,
      ServerSocketConfig& /*config*/) override {

    folly::EventBase* evb = folly::EventBaseManager::get()->getEventBase();
    std::shared_ptr<folly::AsyncUDPServerSocket> socket(
        new folly::AsyncUDPServerSocket(evb),
        ThreadSafeDestructor());
    socket->setReusePort(reuse);
    socket->bind(address);
    socket->listen();

    return socket;
  }

  void removeAcceptCB(std::shared_ptr<folly::AsyncSocketBase> /*s*/,
                      Acceptor* /*callback*/,
                      folly::EventBase* /*base*/) override {
  }

  void addAcceptCB(std::shared_ptr<folly::AsyncSocketBase> s,
                   Acceptor* callback, folly::EventBase* base) override {
    auto socket = std::dynamic_pointer_cast<folly::AsyncUDPServerSocket>(s);
    DCHECK(socket);
    socket->addListener(base, callback);
  }

  class ThreadSafeDestructor {
   public:
    void operator()(folly::AsyncUDPServerSocket* socket) const {
      socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
        [socket]() {
          delete socket;
        }
      );
    }
  };
};

} // namespace wangle

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

#include <folly/futures/Future.h>
#include <folly/Memory.h>

#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/bootstrap/ClientBootstrap.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/AsyncSocketHandler.h>

namespace wangle {

/**
 * A Service is an asynchronous function from Request to
 * Future<Response>. It is the basic unit of the RPC interface.
 */
template <typename Req, typename Resp = Req>
class Service {
 public:
  virtual folly::Future<Resp> operator()(Req request) = 0;
  virtual ~Service() = default;
  virtual folly::Future<folly::Unit> close() {
    return folly::makeFuture();
  }
  virtual bool isAvailable() {
    return true;
  }
};

/**
 * A Filter acts as a decorator/transformer of a service. It may apply
 * transformations to the input and output of that service:
 *
 *          class MyService
 *
 * ReqA  -> |
 *          | -> ReqB
 *          | <- RespB
 * RespA <- |
 *
 * For example, you may have a service that takes Strings and parses
 * them as Ints.  If you want to expose this as a Network Service via
 * Thrift, it is nice to isolate the protocol handling from the
 * business rules. Hence you might have a Filter that converts back
 * and forth between Thrift structs:
 *
 * [ThriftIn -> (String  ->  Int) -> ThriftOut]
 */
template <typename ReqA, typename RespA,
          typename ReqB = ReqA, typename RespB = RespA>
class ServiceFilter : public Service<ReqA, RespA> {
  public:
  explicit ServiceFilter(std::shared_ptr<Service<ReqB, RespB>> service)
      : service_(service) {}
  virtual ~ServiceFilter() = default;

  virtual folly::Future<folly::Unit> close() override {
    return service_->close();
  }

  virtual bool isAvailable() override {
    return service_->isAvailable();
  }

 protected:
  std::shared_ptr<Service<ReqB, RespB>> service_;
};

/**
 * A factory that creates services, given a client.  This lets you
 * make RPC calls on the Service interface over a client's pipeline.
 *
 * Clients can be reused after you are done using the service.
 */
template <typename Pipeline, typename Req, typename Resp>
class ServiceFactory {
 public:
  virtual folly::Future<std::shared_ptr<Service<Req, Resp>>> operator()(
    std::shared_ptr<ClientBootstrap<Pipeline>> client) = 0;

  virtual ~ServiceFactory() = default;

};


template <typename Pipeline, typename Req, typename Resp>
class ConstFactory : public ServiceFactory<Pipeline, Req, Resp> {
 public:
  explicit ConstFactory(std::shared_ptr<Service<Req, Resp>> service)
      : service_(service) {}

  virtual folly::Future<std::shared_ptr<Service<Req, Resp>>> operator()(
    std::shared_ptr<ClientBootstrap<Pipeline>> client) {
    return service_;
  }
 private:
  std::shared_ptr<Service<Req, Resp>> service_;
};

template <typename Pipeline, typename ReqA, typename RespA,
          typename ReqB = ReqA, typename RespB = RespA>
class ServiceFactoryFilter : public ServiceFactory<Pipeline, ReqA, RespA> {
 public:
  explicit ServiceFactoryFilter(
    std::shared_ptr<ServiceFactory<Pipeline, ReqB, RespB>> serviceFactory)
      : serviceFactory_(std::move(serviceFactory)) {}

  virtual ~ServiceFactoryFilter() = default;

 protected:
  std::shared_ptr<ServiceFactory<Pipeline, ReqB, RespB>> serviceFactory_;
};

template <typename Pipeline, typename Req, typename Resp = Req>
class FactoryToService : public Service<Req, Resp> {
 public:
  explicit FactoryToService(
    std::shared_ptr<ServiceFactory<Pipeline, Req, Resp>> factory)
      : factory_(factory) {}
  virtual ~FactoryToService() = default;

  virtual folly::Future<Resp> operator()(Req request) override {
    DCHECK(factory_);
    return ((*factory_)(nullptr)).then(
      [=](std::shared_ptr<Service<Req, Resp>> service)
      {
        return (*service)(std::move(request)).ensure(
          [this]() {
            this->close();
          });
      });
  }

 private:
  std::shared_ptr<ServiceFactory<Pipeline, Req, Resp>> factory_;
};


} // namespace wangle

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

#include <wangle/channel/AsyncSocketHandler.h>

namespace wangle {

template <typename R>
class RoutingDataHandler : public wangle::BytesToBytesHandler {
 public:
  struct RoutingData {
    RoutingData() : bufQueue(folly::IOBufQueue::cacheChainLength()) {}

    R routingData;
    folly::IOBufQueue bufQueue;
  };

  class Callback {
   public:
    virtual ~Callback() {}
    virtual void onRoutingData(uint64_t connId, RoutingData& routingData) = 0;
    virtual void onError(uint64_t connId, folly::exception_wrapper ex) = 0;
  };

  RoutingDataHandler(uint64_t connId, Callback* cob);
  virtual ~RoutingDataHandler() {}

  // BytesToBytesHandler implementation
  void read(Context* ctx, folly::IOBufQueue& q) override;
  void readEOF(Context* ctx) override;
  void readException(Context* ctx, folly::exception_wrapper ex) override;

  /**
   * Parse the routing data from bufQueue into routingData. This
   * will be used to compute the hash for choosing the worker thread.
   *
   * Bytes that need to be passed into the child pipeline (such
   * as additional bytes left in bufQueue not used for parsing)
   * should be moved into RoutingData::bufQueue.
   *
   * @return bool - True on success, false if bufQueue doesn't have
   *                sufficient bytes for parsing
   */
  virtual bool parseRoutingData(folly::IOBufQueue& bufQueue,
                                RoutingData& routingData) = 0;

 private:
  uint64_t connId_;
  Callback* cob_{nullptr};
};

template <typename R>
class RoutingDataHandlerFactory {
 public:
  virtual ~RoutingDataHandlerFactory() {}

  virtual std::shared_ptr<RoutingDataHandler<R>> newHandler(
      uint64_t connId, typename RoutingDataHandler<R>::Callback* cob) = 0;
};

} // namespace wangle

#include <wangle/bootstrap/RoutingDataHandler-inl.h>

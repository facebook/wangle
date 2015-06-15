// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/AsyncSocketHandler.h>

namespace folly { namespace wangle {

class RoutingDataHandler : public folly::wangle::BytesToBytesHandler {
 public:
  struct RoutingData {
    std::string routingData;
    folly::IOBufQueue bufQueue{folly::IOBufQueue::cacheChainLength()};
  };

  class Callback {
   public:
    virtual ~Callback() {}
    virtual void onRoutingData(uint64_t connId, RoutingData routingData) = 0;
    virtual void onError(uint64_t connId) = 0;
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

class RoutingDataHandlerFactory {
 public:
  virtual ~RoutingDataHandlerFactory() {}

  virtual std::unique_ptr<RoutingDataHandler> newHandler(
      uint64_t connId, RoutingDataHandler::Callback* cob) = 0;
};

}} // namespace folly::wangle

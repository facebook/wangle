// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <wangle/channel/broadcast/BroadcastHandler.h>
#include <wangle/channel/broadcast/BroadcastPool.h>
#include <wangle/channel/broadcast/ObservingHandler.h>

namespace folly { namespace wangle {

template <typename T>
class MockSubscriber : public Subscriber<T> {
 public:
  MOCK_METHOD1_T(onNext, void(const T&));
  MOCK_METHOD1(onError, void(const folly::exception_wrapper ex));
  MOCK_METHOD0(onCompleted, void());
};

class MockServerPool : public ServerPool {
 public:
  GMOCK_METHOD0_(, noexcept, , getServer, folly::SocketAddress());
};

template <typename T>
class MockBroadcastPool : public BroadcastPool<T, std::string> {
 public:
  MockBroadcastPool() : BroadcastPool<T, std::string>(nullptr, nullptr) {}

  MOCK_METHOD1_T(getHandler,
                 folly::Future<BroadcastHandler<T>*>(const std::string&));
};

class MockObservingHandler : public ObservingHandler<std::string> {
 public:
  MockObservingHandler()
      : ObservingHandler<std::string>("", nullptr, nullptr) {}

  MOCK_METHOD2(write,
               folly::Future<void>(Context*, std::shared_ptr<folly::IOBuf>));
  MOCK_METHOD1(close, folly::Future<void>(Context*));
  MOCK_METHOD0(newBroadcastPool,
               BroadcastPool<std::unique_ptr<folly::IOBuf>, std::string>*());

  folly::Future<void> write(Context* ctx,
                            std::unique_ptr<folly::IOBuf> buf) override {
    std::shared_ptr<folly::IOBuf> sbuf(buf.release());
    return write(ctx, sbuf);
  }
};

class MockAsyncSocketHandler : public folly::wangle::AsyncSocketHandler {
 public:
  MockAsyncSocketHandler() : AsyncSocketHandler(nullptr) {}

  MOCK_METHOD1(transportActive, void(Context*));
  MOCK_METHOD1(transportInactive, void(Context*));
};

}} // namespace folly::wangle

// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <wangle/channel/broadcast/BroadcastHandler.h>
#include <wangle/channel/broadcast/BroadcastPool.h>

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

}} // namespace folly::wangle

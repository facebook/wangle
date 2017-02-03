/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <gmock/gmock.h>
#include <wangle/client/ssl/SSLSessionCallbacks.h>

namespace wangle {

class MockSSLSessionCallbacks : public SSLSessionCallbacks {
 public:
  GMOCK_METHOD2_(, noexcept,, setSSLSessionInternal,
    void(const std::string&, SSL_SESSION*));

  GMOCK_METHOD1_(, const,, getSSLSessionInternal,
    SSL_SESSION*(const std::string&));

  GMOCK_METHOD1_(, noexcept,, removeSSLSession, bool(const std::string&));

  SSLSessionPtr getSSLSession(
      const std::string& host) const noexcept override {
    return SSLSessionPtr(getSSLSessionInternal(host));
  }

  void setSSLSession(
      const std::string& host,
      SSLSessionPtr session) noexcept override {
    setSSLSessionInternal(host, session.release());
  }
};

}

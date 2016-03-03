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

#include <wangle/client/ssl/SSLSession.h>
#include <wangle/client/ssl/SSLSessionCallbacks.h>

#include <folly/SharedMutex.h>

namespace wangle {

/**
  * A SSL session cache that can be used safely across threads.
  * This is useful for clients who cannot avoid sharing the cache
  * across threads. It uses a read/write lock for efficiency.
  */
class ThreadSafeSSLSessionCache : public SSLSessionCallbacks {
 public:
   explicit ThreadSafeSSLSessionCache(
       std::unique_ptr<SSLSessionCallbacks> delegate) :
     delegate_(std::move(delegate)) {}

   // From SSLSessionCallbacks
   void setSSLSession(
     const std::string& identity, SSLSessionPtr session) noexcept override;
   SSLSessionPtr getSSLSession(
       const std::string& identity) const noexcept override;
   bool removeSSLSession(const std::string& identity) noexcept override;
   bool supportsPersistence() const noexcept override;
   size_t size() const override;

 private:
   std::unique_ptr<SSLSessionCallbacks> delegate_;
   mutable folly::SharedMutex mutex_;
};

}

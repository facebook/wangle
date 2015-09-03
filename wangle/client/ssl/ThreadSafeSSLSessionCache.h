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
     const std::string& hostname, SSLSessionPtr session) noexcept override;
   SSLSessionPtr getSSLSession(
       const std::string& hostname) const noexcept override;
   bool removeSSLSession(const std::string& hostname) noexcept override;
   bool supportsPersistence() const noexcept override;
   size_t size() const override;

 private:
   std::unique_ptr<SSLSessionCallbacks> delegate_;
   mutable folly::SharedMutex mutex_;
};

}

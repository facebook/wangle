/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <wangle/ssl/SSLCacheOptions.h>
#include <wangle/ssl/SSLContextManager.h>
#include <wangle/acceptor/SSLContextSelectionMisc.h>

using std::shared_ptr;
using namespace folly;

namespace wangle {

TEST(SSLContextManagerTest, Test1)
{
  EventBase eventBase;
  SSLContextManager sslCtxMgr(&eventBase, "vip_ssl_context_manager_test_",
                              true, nullptr);
  auto www_facebook_com_ctx = std::make_shared<SSLContext>();
  auto start_facebook_com_ctx = std::make_shared<SSLContext>();
  auto start_abc_facebook_com_ctx = std::make_shared<SSLContext>();
  auto www_facebook_com_ctx_sha1 = std::make_shared<SSLContext>();
  auto start_facebook_com_ctx_sha1 = std::make_shared<SSLContext>();
  auto www_bookface_com_ctx_sha1 = std::make_shared<SSLContext>();

  sslCtxMgr.insertSSLCtxByDomainName(
    "*.facebook.com",
    strlen("*.facebook.com"),
    start_facebook_com_ctx_sha1,
    CertCrypto::SHA1_SIGNATURE);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.facebook.com",
    strlen("www.facebook.com"),
    www_facebook_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.facebook.com",
    strlen("www.facebook.com"),
    www_facebook_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.facebook.com",
    strlen("*.facebook.com"),
    start_facebook_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.abc.facebook.com",
    strlen("*.abc.facebook.com"),
    start_abc_facebook_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.facebook.com",
    strlen("www.facebook.com"),
    www_facebook_com_ctx_sha1,
    CertCrypto::SHA1_SIGNATURE);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.bookface.com",
    strlen("www.bookface.com"),
    www_bookface_com_ctx_sha1,
    CertCrypto::SHA1_SIGNATURE);


  shared_ptr<SSLContext> retCtx;
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.facebook.com"));
  EXPECT_EQ(retCtx, www_facebook_com_ctx);
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("WWW.facebook.com"));
  EXPECT_EQ(retCtx, www_facebook_com_ctx);
  EXPECT_FALSE(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("xyz.facebook.com")));

  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("xyz.facebook.com"));
  EXPECT_EQ(retCtx, start_facebook_com_ctx);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("XYZ.facebook.com"));
  EXPECT_EQ(retCtx, start_facebook_com_ctx);

  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("www.abc.facebook.com"));
  EXPECT_EQ(retCtx, start_abc_facebook_com_ctx);

  // ensure "facebook.com" does not match "*.facebook.com"
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("facebook.com")));
  // ensure "Xfacebook.com" does not match "*.facebook.com"
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("Xfacebook.com")));
  // ensure wildcard name only matches one domain up
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(
        SSLContextKey("abc.xyz.facebook.com")));

  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.facebook.com",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_EQ(retCtx, www_facebook_com_ctx_sha1);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("abc.facebook.com",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_EQ(retCtx, start_facebook_com_ctx_sha1);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("xyz.abc.facebook.com",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_FALSE(retCtx);

  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.bookface.com",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_EQ(retCtx, www_bookface_com_ctx_sha1);
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.bookface.com"));
  EXPECT_EQ(retCtx, www_bookface_com_ctx_sha1);


  eventBase.loop(); // Clean up events before SSLContextManager is destructed
}


// TODO Opensource builds cannot the cert/key paths
TEST(SSLContextManagerTest, DISABLED_TestSessionContextIfSupplied)
{
  EventBase eventBase;
  SSLContextManager sslCtxMgr(&eventBase, "vip_ssl_context_manager_test_",
                              true, nullptr);
  SSLContextConfig ctxConfig;
  ctxConfig.sessionContext = "test";
  ctxConfig.addCertificate(
      "wangle/ssl/test/certs/test.cert.pem",
      "wangle/ssl/test/certs/test.key.pem",
      "");

  SSLCacheOptions cacheOptions;
  SocketAddress addr;

  sslCtxMgr.addSSLContextConfig(
      ctxConfig, cacheOptions, nullptr, addr, nullptr);

  SSLContextKey key("test.com", CertCrypto::BEST_AVAILABLE);
  auto ctx = sslCtxMgr.getSSLCtx(key);
  ASSERT_NE(ctx, nullptr);
  auto sessCtxFromCtx = std::string(
      reinterpret_cast<char*>(ctx->getSSLCtx()->sid_ctx),
      ctx->getSSLCtx()->sid_ctx_length);
  EXPECT_EQ(*ctxConfig.sessionContext, sessCtxFromCtx);
  eventBase.loop();
}

// TODO Opensource builds cannot find cert paths
TEST(SSLContextManagerTest, DISABLED_TestSessionContextIfSessionCacheAbsent)
{
  EventBase eventBase;
  SSLContextManager sslCtxMgr(&eventBase, "vip_ssl_context_manager_test_",
                              true, nullptr);
  SSLContextConfig ctxConfig;
  ctxConfig.sessionContext = "test";
  ctxConfig.sessionCacheEnabled = false;
  ctxConfig.addCertificate(
      "wangle/ssl/test/certs/test.cert.pem",
      "wangle/ssl/test/certs/test.key.pem",
      "");

  SSLCacheOptions cacheOptions;
  SocketAddress addr;

  sslCtxMgr.addSSLContextConfig(
      ctxConfig, cacheOptions, nullptr, addr, nullptr);

  SSLContextKey key("test.com", CertCrypto::BEST_AVAILABLE);
  auto ctx = sslCtxMgr.getSSLCtx(key);
  ASSERT_NE(ctx, nullptr);
  auto sessCtxFromCtx = std::string(
      reinterpret_cast<char*>(ctx->getSSLCtx()->sid_ctx),
      ctx->getSSLCtx()->sid_ctx_length);
  EXPECT_EQ(*ctxConfig.sessionContext, sessCtxFromCtx);
  eventBase.loop();
}

} // namespace wangle

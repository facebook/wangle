/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/ssl/SSLContextManager.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <folly/portability/GTest.h>
#include <glog/logging.h>
#include <wangle/acceptor/SSLContextSelectionMisc.h>
#include <wangle/ssl/SSLCacheOptions.h>
#include <wangle/ssl/ServerSSLContext.h>
#include <wangle/ssl/TLSTicketKeyManager.h>

using std::shared_ptr;
using namespace folly;

namespace wangle {

class SSLContextManagerForTest : public SSLContextManager {
 public:
  using SSLContextManager::SSLContextManager;
  using SSLContextManager::insertSSLCtxByDomainName;
  using SSLContextManager::addServerContext;
};

TEST(SSLContextManagerTest, Test1)
{
  SSLContextManagerForTest sslCtxMgr(
      "vip_ssl_context_manager_test_", true, nullptr);
  auto www_example_com_ctx = std::make_shared<SSLContext>();
  auto start_example_com_ctx = std::make_shared<SSLContext>();
  auto start_abc_example_com_ctx = std::make_shared<SSLContext>();
  auto www_example_com_ctx_sha1 = std::make_shared<SSLContext>();
  auto start_example_com_ctx_sha1 = std::make_shared<SSLContext>();
  auto www_example_org_ctx_sha1 = std::make_shared<SSLContext>();

  sslCtxMgr.insertSSLCtxByDomainName(
    "*.example.com",
    start_example_com_ctx_sha1,
    CertCrypto::SHA1_SIGNATURE);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.example.com",
    www_example_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.example.com",
    www_example_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.example.com",
    start_example_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.abc.example.com",
    start_abc_example_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.example.com",
    www_example_com_ctx_sha1,
    CertCrypto::SHA1_SIGNATURE);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.example.org",
    www_example_org_ctx_sha1,
    CertCrypto::SHA1_SIGNATURE);


  shared_ptr<SSLContext> retCtx;
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.example.com"));
  EXPECT_EQ(retCtx, www_example_com_ctx);
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("WWW.example.com"));
  EXPECT_EQ(retCtx, www_example_com_ctx);
  EXPECT_FALSE(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("xyz.example.com")));

  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("xyz.example.com"));
  EXPECT_EQ(retCtx, start_example_com_ctx);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("XYZ.example.com"));
  EXPECT_EQ(retCtx, start_example_com_ctx);

  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("www.abc.example.com"));
  EXPECT_EQ(retCtx, start_abc_example_com_ctx);

  // ensure "example.com" does not match "*.example.com"
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("example.com")));
  // ensure "Xexample.com" does not match "*.example.com"
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("Xexample.com")));
  // ensure wildcard name only matches one domain up
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(
        SSLContextKey("abc.xyz.example.com")));

  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.example.com",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_EQ(retCtx, www_example_com_ctx_sha1);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("abc.example.com",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_EQ(retCtx, start_example_com_ctx_sha1);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("xyz.abc.example.com",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_FALSE(retCtx);

  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.example.org",
        CertCrypto::SHA1_SIGNATURE));
  EXPECT_EQ(retCtx, www_example_org_ctx_sha1);
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.example.org"));
  EXPECT_EQ(retCtx, www_example_org_ctx_sha1);

}

// This test uses multiple contexts, which requires SNI support to work at all.
#if FOLLY_OPENSSL_HAS_SNI
TEST(SSLContextManagerTest, TestResetSSLContextConfigs) {
  SSLContextManagerForTest sslCtxMgr(
      "vip_ssl_context_manager_test_", true, nullptr);
  SSLCacheOptions cacheOptions;
  SocketAddress addr;

  TLSTicketKeySeeds seeds1{{"67"}, {"68"}, {"69"}};
  TLSTicketKeySeeds seeds2{{"68"}, {"69"}, {"70"}};
  TLSTicketKeySeeds seeds3{{"69"}, {"70"}, {"71"}};

  SSLContextConfig ctxConfig1;
  ctxConfig1.sessionContext = "ctx1";
  ctxConfig1.addCertificate(
      "wangle/ssl/test/certs/test.cert.pem",
      "wangle/ssl/test/certs/test.key.pem",
      "");
  SSLContextConfig ctxConfig1Default = ctxConfig1;
  ctxConfig1Default.isDefault = true;

  SSLContextConfig ctxConfig2;
  ctxConfig2.sessionContext = "ctx2";
  ctxConfig2.addCertificate(
      "wangle/ssl/test/certs/test2.cert.pem",
      "wangle/ssl/test/certs/test2.key.pem",
      "");
  SSLContextConfig ctxConfig2Default = ctxConfig2;
  ctxConfig2Default.isDefault = true;

  SSLContextConfig ctxConfig3;
  ctxConfig3.sessionContext = "ctx3";
  ctxConfig3.addCertificate(
      "wangle/ssl/test/certs/test3.cert.pem",
      "wangle/ssl/test/certs/test3.key.pem",
      "");
  SSLContextConfig ctxConfig3Default = ctxConfig3;
  ctxConfig3Default.isDefault = true;

  // Helper function that verifies seeds are what we expect.
  auto checkSeeds = [](std::shared_ptr<folly::SSLContext> ctx,
                       TLSTicketKeySeeds& seeds) {
    ASSERT_TRUE(ctx);
    auto ticketMgr =
        std::dynamic_pointer_cast<ServerSSLContext>(ctx)->getTicketManager();
    ASSERT_TRUE(ticketMgr);
    TLSTicketKeySeeds fetchedSeeds;
    ticketMgr->getTLSTicketKeySeeds(
        fetchedSeeds.oldSeeds,
        fetchedSeeds.currentSeeds,
        fetchedSeeds.newSeeds);
    EXPECT_EQ(fetchedSeeds, seeds);
  };

  // Reset with just one default
  sslCtxMgr.resetSSLContextConfigs(
      {ctxConfig1Default}, cacheOptions, &seeds1, addr, nullptr);
  EXPECT_EQ(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")),
      sslCtxMgr.getDefaultSSLCtx());
  EXPECT_TRUE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")));
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test2.com")));
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test3.com")));
  checkSeeds(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")), seeds1);

  // Reset with a different set of contexts, no new seeds.
  sslCtxMgr.resetSSLContextConfigs(
      {ctxConfig2Default, ctxConfig3}, cacheOptions, nullptr, addr, nullptr);
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")));
  EXPECT_TRUE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test2.com")));
  EXPECT_TRUE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test3.com")));
  checkSeeds(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test2.com")), seeds1);
  checkSeeds(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test3.com")), seeds1);

  // New set of contexts, new seeds.
  sslCtxMgr.resetSSLContextConfigs(
      {ctxConfig1Default, ctxConfig3}, cacheOptions, &seeds2, addr, nullptr);
  EXPECT_TRUE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")));
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test2.com")));
  EXPECT_TRUE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test3.com")));
  checkSeeds(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")), seeds2);
  checkSeeds(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test3.com")), seeds2);

  // Back to one context, no new seeds.
  sslCtxMgr.resetSSLContextConfigs(
      {ctxConfig1Default}, cacheOptions, nullptr, addr, nullptr);
  EXPECT_TRUE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")));
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test2.com")));
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test3.com")));
  checkSeeds(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")), seeds2);

  // Finally, check that failure doesn't modify anything.
  // This will have new contexts + seeds, but two default contexts set. This
  // should error.
  EXPECT_THROW(
      sslCtxMgr.resetSSLContextConfigs(
          {ctxConfig1Default, ctxConfig2Default, ctxConfig3},
          cacheOptions,
          &seeds3,
          addr,
          nullptr),
      std::runtime_error);
  // These should return the same as the previous successful result
  EXPECT_TRUE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")));
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test2.com")));
  EXPECT_FALSE(sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test3.com")));
  checkSeeds(
      sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("test.com")), seeds2);
}
#endif

#if !(FOLLY_OPENSSL_IS_110) && !defined(OPENSSL_IS_BORINGSSL)
// TODO Opensource builds cannot the cert/key paths
TEST(SSLContextManagerTest, DISABLED_TestSessionContextIfSupplied)
{
  SSLContextManagerForTest sslCtxMgr(
      "vip_ssl_context_manager_test_", true, nullptr);
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
}

// TODO Opensource builds cannot find cert paths
TEST(SSLContextManagerTest, DISABLED_TestSessionContextIfSessionCacheAbsent)
{
  SSLContextManagerForTest sslCtxMgr(
      "vip_ssl_context_manager_test_", true, nullptr);
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
}
#endif

TEST(SSLContextManagerTest, TestSessionContextCertRemoval)
{
  SSLContextManagerForTest sslCtxMgr(
      "vip_ssl_context_manager_test_", true, nullptr);
  auto www_example_com_ctx = std::make_shared<ServerSSLContext>();
  auto start_example_com_ctx = std::make_shared<ServerSSLContext>();
  auto start_abc_example_com_ctx = std::make_shared<ServerSSLContext>();
  auto www_abc_example_com_ctx = std::make_shared<ServerSSLContext>();

  sslCtxMgr.insertSSLCtxByDomainName(
    "www.example.com",
    www_example_com_ctx);
  sslCtxMgr.addServerContext(www_example_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.example.com",
    start_example_com_ctx);
  sslCtxMgr.addServerContext(start_example_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.abc.example.com",
    start_abc_example_com_ctx);
  sslCtxMgr.addServerContext(start_abc_example_com_ctx);

  shared_ptr<SSLContext> retCtx;
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.example.com"));
  EXPECT_EQ(retCtx, www_example_com_ctx);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("www.abc.example.com"));
  EXPECT_EQ(retCtx, start_abc_example_com_ctx);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(SSLContextKey("xyz.example.com"));
  EXPECT_EQ(retCtx, start_example_com_ctx);

  // Removing one of the contexts
  sslCtxMgr.removeSSLContextConfig(SSLContextKey("www.example.com"));
  retCtx = sslCtxMgr.getSSLCtxByExactDomain(SSLContextKey("www.example.com"));
  EXPECT_FALSE(retCtx);

  // If the wildcard context is successfully removed, there should be no context
  // for a random domain that is of the form *.example.com.
  sslCtxMgr.removeSSLContextConfig(SSLContextKey(".example.com"));
  retCtx = sslCtxMgr.getSSLCtx(SSLContextKey("foo.example.com"));
  EXPECT_FALSE(retCtx);

  // Add it back and delete again but with the other API.
  sslCtxMgr.insertSSLCtxByDomainName("*.example.com", start_example_com_ctx);
  sslCtxMgr.addServerContext(start_example_com_ctx);
  retCtx = sslCtxMgr.getSSLCtx(SSLContextKey("foo.example.com"));
  EXPECT_TRUE(retCtx);
  sslCtxMgr.removeSSLContextConfigByDomainName("*.example.com");
  retCtx = sslCtxMgr.getSSLCtx(SSLContextKey("foo.example.com"));
  EXPECT_FALSE(retCtx);

  // Try to remove the context which does not exist - must be NOOP
  sslCtxMgr.removeSSLContextConfig(SSLContextKey("xyz.example.com"));

  // Setting a default context
  sslCtxMgr.insertSSLCtxByDomainName(
      "www.abc.example.com",
      www_abc_example_com_ctx,
      CertCrypto::BEST_AVAILABLE,
      true);

  // Context Manager must throw on attempt to remove the default context
  EXPECT_THROW(
      sslCtxMgr.removeSSLContextConfig(SSLContextKey("www.abc.example.com")),
      std::invalid_argument);
}

} // namespace wangle

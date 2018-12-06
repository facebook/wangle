/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/acceptor/FizzConfigUtil.h>

#include <fizz/protocol/DefaultCertificateVerifier.h>
#include <folly/Format.h>

using fizz::CertUtils;
using fizz::DefaultCertificateVerifier;
using fizz::FizzUtil;
using fizz::ProtocolVersion;
using fizz::PskKeyExchangeMode;
using fizz::VerificationContext;
using fizz::server::ClientAuthMode;

namespace wangle {

std::unique_ptr<fizz::server::CertManager>
FizzConfigUtil::createCertManager(const ServerSocketConfig& config) {
  auto certMgr = std::make_unique<fizz::server::CertManager>();
  auto loadedCert = false;
  for (const auto& sslConfig : config.sslContextConfigs) {
    for (const auto& cert : sslConfig.certificates) {
      try {
        auto x509Chain = FizzUtil::readChainFile(cert.certPath);
        auto pkey = FizzUtil::readPrivateKey(cert.keyPath, cert.passwordPath);
        auto selfCert =
            CertUtils::makeSelfCert(std::move(x509Chain), std::move(pkey));
        certMgr->addCert(std::move(selfCert), sslConfig.isDefault);
        loadedCert = true;
      } catch (const std::runtime_error& ex) {
        auto msg = folly::sformat(
            "Failed to load cert or key at key path {}, cert path {}",
            cert.keyPath,
            cert.certPath);
        if (config.strictSSL) {
          throw std::runtime_error(ex.what() + msg);
        } else {
          LOG(ERROR) << msg << ex.what();
        }
      }
    }
  }
  if (!loadedCert) {
    return nullptr;
  }
  return certMgr;
}

std::shared_ptr<fizz::server::FizzServerContext>
FizzConfigUtil::createFizzContext(const ServerSocketConfig& config) {
  if (config.sslContextConfigs.empty()) {
    return nullptr;
  }
  auto ctx = std::make_shared<fizz::server::FizzServerContext>();
  ctx->setSupportedVersions({ProtocolVersion::tls_1_3,
                             ProtocolVersion::tls_1_3_28,
                             ProtocolVersion::tls_1_3_26});
  ctx->setVersionFallbackEnabled(true);

  // Fizz does not yet support randomized next protocols so we use the highest
  // weighted list on the first context.
  const auto& list = config.sslContextConfigs.front().nextProtocols;
  if (!list.empty()) {
    ctx->setSupportedAlpns(FizzUtil::getAlpnsFromNpnList(list));
  }

  auto verify = config.sslContextConfigs.front().clientVerification;
  switch (verify) {
    case folly::SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT:
      ctx->setClientAuthMode(ClientAuthMode::Required);
      break;
    case folly::SSLContext::SSLVerifyPeerEnum::VERIFY:
      ctx->setClientAuthMode(ClientAuthMode::Optional);
      break;
    default:
      ctx->setClientAuthMode(ClientAuthMode::None);
  }

  auto caFile = config.sslContextConfigs.front().clientCAFile;
  if (!caFile.empty()) {
    try {
      auto verifier = DefaultCertificateVerifier::createFromCAFile(
          VerificationContext::Server, caFile);
      ctx->setClientCertVerifier(std::move(verifier));
    } catch (const std::runtime_error& ex) {
      auto msg = folly::sformat(" Failed to load ca file at {}", caFile);
      if (config.strictSSL) {
        throw std::runtime_error(ex.what() + msg);
      } else {
        LOG(ERROR) << msg << ex.what();
        return nullptr;
      }
    }
  }

  return ctx;
}

} // namespace wangle

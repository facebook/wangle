/*
 * Copyright 2017-present Facebook, Inc.
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
#pragma once

#include <folly/Optional.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/SSLOptions.h>
#include <set>
#include <string>
#include <vector>

/**
 * SSLContextConfig helps to describe the configs/options for
 * a SSL_CTX. For example:
 *
 *   1. Filename of X509, private key and its password.
 *   2. ciphers list
 *   3. NPN list
 *   4. Is session cache enabled?
 *   5. Is it the default X509 in SNI operation?
 *   6. .... and a few more
 */
namespace wangle {

struct SSLContextConfig {
  SSLContextConfig() = default;
  ~SSLContextConfig() = default;

  struct CertificateInfo {
    CertificateInfo(const std::string& crtPath,
                    const std::string& kyPath,
                    const std::string& passwdPath)
        : certPath(crtPath), keyPath(kyPath), passwordPath(passwdPath) {}
    std::string certPath;
    std::string keyPath;
    std::string passwordPath;
  };

  static const std::string& getDefaultCiphers() {
    static const std::string& defaultCiphers =
        folly::join(':', folly::ssl::SSLServerOptions::kCipherList);
    return defaultCiphers;
  }

  struct KeyOffloadParams {
    // What keys do we want to offload
    // Currently supported values: "rsa", "ec" (can also be empty)
    // Note that the corresponding thrift IDL has a list instead
    std::set<std::string> offloadType;
    // Whether this set of keys need local fallback
    bool localFallback{false};
    // An identifier for the service to which we are offloading.
    std::string serviceId{"default"};
    // Whether we want to offload certificates
    bool enableCertOffload{false};
  };

  /**
   * Helpers to set/add a certificate
   */
  void setCertificate(const std::string& certPath,
                      const std::string& keyPath,
                      const std::string& passwordPath) {
    certificates.clear();
    addCertificate(certPath, keyPath, passwordPath);
  }

  void addCertificate(const std::string& certPath,
                      const std::string& keyPath,
                      const std::string& passwordPath) {
    certificates.emplace_back(certPath, keyPath, passwordPath);
  }

  /**
   * Set the optional list of protocols to advertise via TLS
   * Next Protocol Negotiation. An empty list means NPN is not enabled.
   */
  void setNextProtocols(const std::list<std::string>& inNextProtocols) {
    nextProtocols.clear();
    nextProtocols.emplace_back(1, inNextProtocols);
  }

  typedef std::function<bool(char const* server_name)> SNINoMatchFn;

  std::vector<CertificateInfo> certificates;
  folly::SSLContext::SSLVersion sslVersion{
    folly::SSLContext::TLSv1};
  bool sessionCacheEnabled{true};
  bool sessionTicketEnabled{true};
  bool clientHelloParsingEnabled{true};
  std::string sslCiphers{getDefaultCiphers()};
  std::string eccCurveName{"prime256v1"};

  // Weighted lists of NPN strings to advertise
  std::list<folly::SSLContext::NextProtocolsItem>
      nextProtocols;
  bool isLocalPrivateKey{true};
  // Should this SSLContextConfig be the default for SNI purposes
  bool isDefault{false};
  // Callback function to invoke when there are no matching certificates
  // (will only be invoked once)
  SNINoMatchFn sniNoMatchFn;
  // File containing trusted CA's to validate client certificates
  std::string clientCAFile;
  // Verification method to use for client certificates.
  folly::SSLContext::SSLVerifyPeerEnum clientVerification{
    folly::SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT};
  // Key offload configuration
  KeyOffloadParams keyOffloadParams;
  // A namespace to use for sessions generated from this context so that
  // they will not be shared between other sessions generated from the
  // same context. If not specified the common name for the certificates set
  // in the context will be used by default.
  folly::Optional<std::string> sessionContext;
};

} // namespace wangle

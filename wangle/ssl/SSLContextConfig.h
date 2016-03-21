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

#include <string>
#include <folly/Optional.h>
#include <folly/io/async/SSLContext.h>
#include <vector>
#include <set>

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

  struct KeyOffloadParams {
    // What keys do we want to offload
    // Currently supported values: "rsa", "ec" (can also be empty)
    // Note that the corresponding thrift IDL has a list instead
    std::set<std::string> offloadType;
    // Whether this set of keys need local fallback
    bool localFallback{false};
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
  std::string sslCiphers{
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES128-SHA:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:"
    "AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA:AES256-SHA:"};
  std::string eccCurveName{"prime256v1"};
  // Ciphers to negotiate if TLS version >= 1.1
  std::string tls11Ciphers{""};
  // Knobs to tune ciphersuite picking probability for TLS >= 1.1
  std::vector<std::pair<std::string, int>> tls11AltCipherlist;
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

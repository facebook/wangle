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
#include <wangle/ssl/SSLUtil.h>

#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/io/async/AsyncSSLSocket.h>

#if OPENSSL_VERSION_NUMBER >= 0x1000105fL
#define OPENSSL_GE_101 1
#include <openssl/asn1.h>
#include <openssl/x509v3.h>
#include <openssl/bio.h>
#else
#undef OPENSSL_GE_101
#endif

namespace wangle {

SSLException::SSLException(
      SSLErrorEnum const error,
      std::chrono::milliseconds const& latency,
      uint64_t const bytesRead)
      : std::runtime_error(folly::sformat(
            "SSL error: {}; Elapsed time: {} ms; Bytes read: {}",
            static_cast<int>(error),
            latency.count(),
            bytesRead)),
        error_(error), latency_(latency), bytesRead_(bytesRead) {}

std::mutex SSLUtil::sIndexLock_;

SSLResumeEnum SSLUtil::getResumeState(folly::AsyncSSLSocket* sslSocket) {
  return sslSocket->getSSLSessionReused() ?
    (sslSocket->sessionIDResumed() ?
     SSLResumeEnum::RESUME_SESSION_ID :
     SSLResumeEnum::RESUME_TICKET) :
    SSLResumeEnum::HANDSHAKE;
}

std::unique_ptr<std::string> SSLUtil::getCommonName(const X509* cert) {
  X509_NAME* subject = X509_get_subject_name((X509*)cert);
  if (!subject) {
    return nullptr;
  }
  char cn[ub_common_name + 1];
  int res = X509_NAME_get_text_by_NID(subject, NID_commonName,
                                      cn, ub_common_name);
  if (res <= 0) {
    return nullptr;
  } else {
    cn[ub_common_name] = '\0';
    return std::make_unique<std::string>(cn);
  }
}

std::unique_ptr<std::list<std::string>> SSLUtil::getSubjectAltName(
    const X509* cert) {
#ifdef OPENSSL_GE_101
  auto nameList = std::make_unique<std::list<std::string>>();
  GENERAL_NAMES* names = (GENERAL_NAMES*)X509_get_ext_d2i(
      (X509*)cert, NID_subject_alt_name, nullptr, nullptr);
  if (names) {
    auto guard = folly::makeGuard([names] { GENERAL_NAMES_free(names); });
    size_t count = sk_GENERAL_NAME_num(names);
    CHECK(count < std::numeric_limits<int>::max());
    for (int i = 0; i < (int)count; ++i) {
      GENERAL_NAME* generalName = sk_GENERAL_NAME_value(names, i);
      if (generalName->type == GEN_DNS) {
        ASN1_STRING* s = generalName->d.dNSName;
        const char* name = (const char*)ASN1_STRING_get0_data(s);
        // I can't find any docs on what a negative return value here
        // would mean, so I'm going to ignore it.
        auto len = ASN1_STRING_length(s);
        DCHECK(len >= 0);
        if (size_t(len) != strlen(name)) {
          // Null byte(s) in the name; return an error rather than depending on
          // the caller to safely handle this case.
          return nullptr;
        }
        nameList->emplace_back(name);
      }
    }
  }
  return nameList;
#else
  return nullptr;
#endif
}

folly::ssl::X509UniquePtr SSLUtil::getX509FromCertificate(
    const std::string& certificateData) {
  // BIO_new_mem_buf creates a bio pointing to a read-only buffer. However,
  // older versions of OpenSSL fail to mark the first argument `const`.
  folly::ssl::BioUniquePtr bio(
    BIO_new_mem_buf((void*)certificateData.data(), certificateData.length()));
  if (!bio) {
    throw std::runtime_error("Cannot create mem BIO");
  }

  auto x509 = folly::ssl::X509UniquePtr(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!x509) {
    throw std::runtime_error("Cannot read X509 from PEM bio");
  }
  return x509;
}

} // namespace wangle

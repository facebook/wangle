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

#include <folly/String.h>
#include <mutex>
#include <folly/io/async/AsyncSSLSocket.h>

namespace wangle {

/**
 * SSL session establish/resume status
 *
 * changing these values will break logging pipelines
 */
enum class SSLResumeEnum : uint8_t {
  HANDSHAKE = 0,
  RESUME_SESSION_ID = 1,
  RESUME_TICKET = 3,
  NA = 2
};

enum class SSLErrorEnum {
  NO_ERROR,
  TIMEOUT,
  DROPPED
};

class SSLException : public std::exception {
 public:
  SSLException(SSLErrorEnum error,
               const std::chrono::milliseconds& latency,
               uint64_t bytesRead)
      : error_(error), latency_(latency), bytesRead_(bytesRead) {}

  SSLErrorEnum getError() const { return error_; }
  std::chrono::milliseconds getLatency() const { return latency_; }
  uint64_t getBytesRead() const { return bytesRead_; }

  const char* what() const noexcept {
    const auto& err = folly::sformat(
        "SSL error: {}; Elapsed time: {} ms; Bytes read: {}",
        folly::to<int>(error_),
        latency_.count(),
        bytesRead_);
    return err.c_str();
  }

 private:
  SSLErrorEnum error_{SSLErrorEnum::NO_ERROR};
  std::chrono::milliseconds latency_;
  uint64_t bytesRead_{0};
};

class SSLUtil {
 private:
  static std::mutex sIndexLock_;

 public:
  /**
   * Ensures only one caller will allocate an ex_data index for a given static
   * or global.
   */
  static void getSSLCtxExIndex(int* pindex) {
    std::lock_guard<std::mutex> g(sIndexLock_);
    if (*pindex < 0) {
      *pindex = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    }
  }

  static void getRSAExIndex(int* pindex) {
    std::lock_guard<std::mutex> g(sIndexLock_);
    if (*pindex < 0) {
      *pindex = RSA_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    }
  }

  // ex data string dup func
  static int exDataStdStringDup(
      CRYPTO_EX_DATA* /* to */,
      CRYPTO_EX_DATA* /* from */,
      void* ptr,
      int /* idx */,
      long /* argl */,
      void* /* argp */) {
    // TODO: This is awful - ptr is actually a void** and needs to be set to the
    // duped data.  Fix this when openssl fixes their API - see
    // int_dup_ex_data in ex_data.c
    void** dataPtr = reinterpret_cast<void**>(ptr);
    std::string* strData = reinterpret_cast<std::string*>(*dataPtr);
    if (strData) {
      *dataPtr = new std::string(*strData);
    }
    return 1;
  }

  // ex data string free func
  static void exDataStdStringFree(
      void* /* parent */,
      void* ptr,
      CRYPTO_EX_DATA* /* ad */,
      int /* idx */,
      long /* argl */,
      void* /* argp */) {
    if (ptr) {
      auto strPtr = reinterpret_cast<std::string*>(ptr);
      delete strPtr;
    }
  }
  // get an index that will store a std::string*
  static void getSSLSessionExStrIndex(int* pindex) {
    std::lock_guard<std::mutex> g(sIndexLock_);
    if (*pindex < 0) {
      *pindex = SSL_SESSION_get_ex_new_index(
        0, nullptr, nullptr, exDataStdStringDup, exDataStdStringFree);
    }
  }

  static inline std::string hexlify(const std::string& binary) {
    std::string hex;
    folly::hexlify<std::string, std::string>(binary, hex);

    return hex;
  }

  static inline const std::string& hexlify(const std::string& binary,
                                           std::string& hex) {
    folly::hexlify<std::string, std::string>(binary, hex);

    return hex;
  }

  /**
   * Return the SSL resume type for the given socket.
   */
  static inline SSLResumeEnum getResumeState(
    folly::AsyncSSLSocket* sslSocket) {
    return sslSocket->getSSLSessionReused() ?
      (sslSocket->sessionIDResumed() ?
        SSLResumeEnum::RESUME_SESSION_ID :
        SSLResumeEnum::RESUME_TICKET) :
      SSLResumeEnum::HANDSHAKE;
  }

  /**
   * Get the Common Name from an X.509 certificate
   * @param cert  certificate to inspect
   * @return  common name, or null if an error occurs
   */
  static std::unique_ptr<std::string> getCommonName(const X509* cert);

  /**
   * Get the Subject Alternative Name value(s) from an X.509 certificate
   * @param cert  certificate to inspect
   * @return  set of zero or more alternative names, or null if
   *            an error occurs
   */
  static std::unique_ptr<std::list<std::string>> getSubjectAltName(
      const X509* cert);
};

} // namespace wangle

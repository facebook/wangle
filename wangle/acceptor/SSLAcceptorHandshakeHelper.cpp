/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/SSLAcceptorHandshakeHelper.h>

#include <string>
#include <wangle/acceptor/Acceptor.h>
#include <wangle/acceptor/SecureTransportType.h>

namespace wangle {

static const std::string empty_string;

using namespace folly;

void SSLAcceptorHandshakeHelper::start(
    folly::AsyncSSLSocket::UniquePtr sock,
    AcceptorHandshakeHelper::Callback* callback) noexcept {
  socket_ = std::move(sock);
  callback_ = callback;

  socket_->enableClientHelloParsing();
  socket_->forceCacheAddrOnFailure(true);
  socket_->sslAccept(this);
}

void SSLAcceptorHandshakeHelper::fillSSLTransportInfoFields(
    AsyncSSLSocket* sock, TransportInfo& tinfo) {
  tinfo.secure = true;
  tinfo.securityType = sock->getSecurityProtocol();
  tinfo.sslSetupBytesRead = sock->getRawBytesReceived();
  tinfo.sslSetupBytesWritten = sock->getRawBytesWritten();
  tinfo.sslServerName = sock->getSSLServerName() ?
    std::make_shared<std::string>(sock->getSSLServerName()) : nullptr;
  tinfo.sslCipher = sock->getNegotiatedCipherName() ?
    std::make_shared<std::string>(sock->getNegotiatedCipherName()) : nullptr;
  tinfo.sslVersion = sock->getSSLVersion();
  const char* sigAlgName = sock->getSSLCertSigAlgName();
  tinfo.sslCertSigAlgName =
    std::make_shared<std::string>(sigAlgName ? sigAlgName : "");
  tinfo.sslCertSize = sock->getSSLCertSize();
  tinfo.sslResume = SSLUtil::getResumeState(sock);
  tinfo.sslClientCiphers = std::make_shared<std::string>();
  sock->getSSLClientCiphers(*tinfo.sslClientCiphers);
  tinfo.sslClientCiphersHex = std::make_shared<std::string>();
  sock->getSSLClientCiphers(
      *tinfo.sslClientCiphersHex, /* convertToString = */ false);
  tinfo.sslServerCiphers = std::make_shared<std::string>();
  sock->getSSLServerCiphers(*tinfo.sslServerCiphers);
  tinfo.sslClientComprMethods =
      std::make_shared<std::string>(sock->getSSLClientComprMethods());
  tinfo.sslClientExts =
      std::make_shared<std::string>(sock->getSSLClientExts());
  tinfo.sslClientSigAlgs =
      std::make_shared<std::string>(sock->getSSLClientSigAlgs());
  tinfo.sslClientSupportedVersions =
      std::make_shared<std::string>(sock->getSSLClientSupportedVersions());
}

void SSLAcceptorHandshakeHelper::handshakeSuc(AsyncSSLSocket* sock) noexcept {
  const unsigned char* nextProto = nullptr;
  unsigned nextProtoLength = 0;
  sock->getSelectedNextProtocolNoThrow(&nextProto, &nextProtoLength);
  if (VLOG_IS_ON(3)) {
    if (nextProto) {
      VLOG(3) << "Client selected next protocol " <<
          std::string((const char*)nextProto, nextProtoLength);
    } else {
      VLOG(3) << "Client did not select a next protocol";
    }
  }

  // fill in SSL-related fields from TransportInfo
  // the other fields like RTT are filled in the Acceptor
  tinfo_.acceptTime = acceptTime_;
  tinfo_.sslSetupTime = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - acceptTime_
  );
  fillSSLTransportInfoFields(sock, tinfo_);

  auto nextProtocol = nextProto ?
    std::string((const char*)nextProto, nextProtoLength) : empty_string;

  // The callback will delete this.
  callback_->connectionReady(
      std::move(socket_),
      std::move(nextProtocol),
      SecureTransportType::TLS,
      SSLErrorEnum::NO_ERROR);
}

void SSLAcceptorHandshakeHelper::handshakeErr(
    AsyncSSLSocket* sock,
    const AsyncSocketException& ex) noexcept {
  auto elapsedTime =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - acceptTime_);
  VLOG(3) << "SSL handshake error after " << elapsedTime.count() <<
      " ms; " << sock->getRawBytesReceived() << " bytes received & " <<
      sock->getRawBytesWritten() << " bytes sent: " <<
      ex.what();

  auto sslEx = folly::make_exception_wrapper<SSLException>(
      sslError_, elapsedTime, sock->getRawBytesReceived());

  // The callback will delete this.
  callback_->connectionError(socket_.get(), sslEx, sslError_);
}

}

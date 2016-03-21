/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/SSLAcceptorHandshakeHelper.h>

#include <string>
#include <wangle/acceptor/SecureTransportType.h>

namespace wangle {

static const std::string empty_string;

using namespace folly;

void SSLAcceptorHandshakeHelper::start(
    folly::AsyncSSLSocket::UniquePtr sock,
    AcceptorHandshakeHelper::Callback* callback) noexcept {
  socket_ = std::move(sock);
  callback_ = callback;

  if (acceptor_->getParseClientHello()) {
    socket_->enableClientHelloParsing();
  }

  socket_->forceCacheAddrOnFailure(true);
  socket_->sslAccept(this);
}

void SSLAcceptorHandshakeHelper::handshakeSuc(AsyncSSLSocket* sock) noexcept {
  const unsigned char* nextProto = nullptr;
  unsigned nextProtoLength = 0;
  sock->getSelectedNextProtocol(&nextProto, &nextProtoLength);
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
  tinfo_.ssl = true;
  tinfo_.acceptTime = acceptTime_;
  tinfo_.sslSetupTime = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - acceptTime_
  );
  tinfo_.sslSetupBytesRead = sock->getRawBytesReceived();
  tinfo_.sslSetupBytesWritten = sock->getRawBytesWritten();
  tinfo_.sslServerName = sock->getSSLServerName() ?
    std::make_shared<std::string>(sock->getSSLServerName()) : nullptr;
  tinfo_.sslCipher = sock->getNegotiatedCipherName() ?
    std::make_shared<std::string>(sock->getNegotiatedCipherName()) : nullptr;
  tinfo_.sslVersion = sock->getSSLVersion();
  const char* sigAlgName = sock->getSSLCertSigAlgName();
  tinfo_.sslCertSigAlgName =
    std::make_shared<std::string>(sigAlgName ? sigAlgName : "");
  tinfo_.sslCertSize = sock->getSSLCertSize();
  tinfo_.sslResume = SSLUtil::getResumeState(sock);
  tinfo_.sslClientCiphers = std::make_shared<std::string>();
  sock->getSSLClientCiphers(*tinfo_.sslClientCiphers);
  tinfo_.sslClientCiphersHex = std::make_shared<std::string>();
  sock->getSSLClientCiphers(
      *tinfo_.sslClientCiphersHex, /* convertToString = */ false);
  tinfo_.sslServerCiphers = std::make_shared<std::string>();
  sock->getSSLServerCiphers(*tinfo_.sslServerCiphers);
  tinfo_.sslClientComprMethods =
      std::make_shared<std::string>(sock->getSSLClientComprMethods());
  tinfo_.sslClientExts =
      std::make_shared<std::string>(sock->getSSLClientExts());
  tinfo_.sslClientSigAlgs =
      std::make_shared<std::string>(sock->getSSLClientSigAlgs());
  tinfo_.sslNextProtocol = std::make_shared<std::string>();
  tinfo_.sslNextProtocol->assign(reinterpret_cast<const char*>(nextProto),
                                nextProtoLength);

  acceptor_->updateSSLStats(
    sock,
    tinfo_.sslSetupTime,
    SSLErrorEnum::NO_ERROR
  );
  auto nextProtocol = nextProto ?
    std::string((const char*)nextProto, nextProtoLength) : empty_string;

  // The callback will delete this.
  callback_->connectionReady(
      std::move(socket_),
      std::move(nextProtocol),
      SecureTransportType::TLS);
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
  acceptor_->updateSSLStats(sock, elapsedTime, sslError_);
  auto sslEx = folly::make_exception_wrapper<SSLException>(
      sslError_, elapsedTime, sock->getRawBytesReceived());

  // The callback will delete this.
  callback_->connectionError(sslEx);
}

}

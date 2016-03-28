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

namespace wangle {

class SSLStats {
 public:
  virtual ~SSLStats() noexcept {}

  // downstream
  virtual void recordSSLAcceptLatency(int64_t latency) noexcept = 0;
  virtual void recordTLSTicket(bool ticketNew, bool ticketHit) noexcept = 0;
  virtual void recordSSLSession(bool sessionNew, bool sessionHit, bool foreign)
    noexcept = 0;
  virtual void recordSSLSessionRemove() noexcept = 0;
  virtual void recordSSLSessionFree(uint32_t freed) noexcept = 0;
  virtual void recordSSLSessionSetError(uint32_t err) noexcept = 0;
  virtual void recordSSLSessionGetError(uint32_t err) noexcept = 0;
  virtual void recordClientRenegotiation() noexcept = 0;

  // upstream
  virtual void recordSSLUpstreamConnection(bool handshake) noexcept = 0;
  virtual void recordSSLUpstreamConnectionError(bool verifyError) noexcept = 0;

};

} // namespace wangle

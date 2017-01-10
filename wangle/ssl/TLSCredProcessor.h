/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include <wangle/ssl/TLSTicketKeySeeds.h>
#include <wangle/util/FilePoller.h>

namespace wangle {

class TLSCredProcessor {
 public:
  TLSCredProcessor(const std::string& ticketFile,
                   const std::string& certFile);

  ~TLSCredProcessor();

  void addTicketCallback(
      std::function<void(wangle::TLSTicketKeySeeds)> callback);
  void addCertCallback(std::function<void()> callback);

  void ticketFileUpdated() noexcept;
  void certFileUpdated() noexcept;

  void stop();

  /**
   * This parses a TLS ticket file with the tickets and returns a
   * TLSTicketKeySeeds structure if the file is valid.
   * The TLS ticket file is formatted as a json blob
   * {
   *   "old": [
   *     "seed1",
   *     ...
   *   ],
   *   "new": [
   *     ...
   *   ],
   *   "current": [
   *     ...
   *   ]
   * }
   * Seeds are aribitrary length secret strings which are used to derive
   * ticket encryption keys.
   */
  static folly::Optional<wangle::TLSTicketKeySeeds> processTLSTickets(
      const std::string& fileName);

  const std::string ticketFile_;
  const std::string certFile_;
  std::unique_ptr<FilePoller> poller_;
  std::vector<std::function<void(wangle::TLSTicketKeySeeds)>> ticketCallbacks_;
  std::vector<std::function<void()>> certCallbacks_;
};
}

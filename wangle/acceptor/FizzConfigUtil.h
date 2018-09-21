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

#pragma once

#include <fizz/server/FizzServerContext.h>
#include <fizz/util/FizzUtil.h>

#include <wangle/acceptor/ServerSocketConfig.h>

namespace wangle {

class FizzConfigUtil {
 public:
  static std::shared_ptr<fizz::server::FizzServerContext> createFizzContext(
      const wangle::ServerSocketConfig& config,
      std::unique_ptr<fizz::server::CertManager> certMgr = nullptr);

  // Creates a TicketCipher with given params
  template <class TicketCipher>
  static std::unique_ptr<TicketCipher> createTicketCipher(
      const std::vector<std::string>& oldSecrets,
      const std::vector<std::string>& currentSecrets,
      const std::vector<std::string>& newSecrets,
      std::chrono::seconds validity,
      folly::Optional<std::string> pskContext) {
    if (currentSecrets.empty()) {
      return fizz::FizzUtil::createTicketCipher<TicketCipher>(
          oldSecrets, "", newSecrets, validity, std::move(pskContext));
    } else {
      return fizz::FizzUtil::createTicketCipher<TicketCipher>(
          oldSecrets,
          currentSecrets.at(0),
          newSecrets,
          validity,
          std::move(pskContext));
    }
  }
};

} // namespace wangle

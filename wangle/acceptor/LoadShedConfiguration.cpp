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
#include <wangle/acceptor/LoadShedConfiguration.h>

#include <folly/Conv.h>
#include <openssl/ssl.h>

using std::string;
using folly::SocketAddress;

namespace wangle {

void LoadShedConfiguration::addWhitelistAddr(folly::StringPiece input) {
  auto addr = input.str();
  size_t separator = addr.find_first_of('/');
  if (separator == string::npos) {
    whitelistAddrs_.insert(SocketAddress(addr, 0));
  } else {
    unsigned prefixLen = folly::to<unsigned>(addr.substr(separator + 1));
    addr.erase(separator);
    whitelistNetworks_.insert(NetworkAddress(SocketAddress(addr, 0), prefixLen));
  }
}

bool LoadShedConfiguration::isWhitelisted(const SocketAddress& address) const {
  if (whitelistAddrs_.find(address) != whitelistAddrs_.end()) {
    return true;
  }
  for (auto& network : whitelistNetworks_) {
    if (network.contains(address)) {
      return true;
    }
  }
  return false;
}

} // namespace wangle

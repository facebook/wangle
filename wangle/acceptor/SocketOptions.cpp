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
#include <wangle/acceptor/SocketOptions.h>

#include <folly/portability/Sockets.h>

using folly::AsyncSocket;

namespace wangle {

AsyncSocket::OptionMap filterIPSocketOptions(
  const AsyncSocket::OptionMap& allOptions,
  const int addrFamily) {
  AsyncSocket::OptionMap opts;
  int exclude;
  if (addrFamily == AF_INET) {
    exclude = IPPROTO_IPV6;
  } else if (addrFamily == AF_INET6) {
    exclude = IPPROTO_IP;
  } else {
    LOG(FATAL) << "Address family " << addrFamily << " was not IPv4 or IPv6";
  }
  for (const auto& opt: allOptions) {
    if (opt.first.level != exclude) {
      opts[opt.first] = opt.second;
    }
  }
  return opts;
}

} // namespace wangle

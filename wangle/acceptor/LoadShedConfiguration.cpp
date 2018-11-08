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
#include <folly/portability/OpenSSL.h>

using folly::SocketAddress;
using std::string;

namespace wangle {

void LoadShedConfiguration::addWhitelistAddr(folly::StringPiece input) {
  auto addr = input.str();
  size_t separator = addr.find_first_of('/');
  if (separator == string::npos) {
    whitelistAddrs_.insert(SocketAddress(addr, 0));
  } else {
    unsigned prefixLen = folly::to<unsigned>(addr.substr(separator + 1));
    addr.erase(separator);
    whitelistNetworks_.insert(
        NetworkAddress(SocketAddress(addr, 0), prefixLen));
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

void LoadShedConfiguration::checkIsSane(const SysParams& sysParams) const {
  if (loadSheddingEnabled_) {
    // Period must be greater than or equal to 0.
    CHECK_GE(period_.count(), std::chrono::milliseconds(0).count());

    // CPU exceed window must be of size at least equal to 1.
    CHECK_GE(cpuUsageExceedWindowSize_, 1);

    // Min cpu idle and max cpu ratios must have values in the range of [0-1]
    // inclusive and min cpu idle, normalized, must be greater than or equal
    // to max cpu ratio.
    CHECK_GE(minCpuIdle_, 0.0);
    CHECK_LE(minCpuIdle_, 1.0);
    CHECK_GE(1.0 - minCpuIdle_, maxCpuUsage_);
    CHECK_GE(maxCpuUsage_, 0.0);
    CHECK_LE(maxCpuUsage_, 1.0);
    CHECK_GE(logicalCpuCoreQuorum_, 0);
    CHECK_LE(logicalCpuCoreQuorum_, sysParams.numLogicalCpuCores);

    // Max mem usage must be less than or equal to min free mem, normalized.
    // We also must verify that min free mem is less than or equal to total
    // mem bytes.
    CHECK_GE(maxMemUsage_, 0.0);
    CHECK_LE(maxMemUsage_, 1.0);
    CHECK_GE(
        1.0 - ((double)minFreeMem_ / sysParams.totalMemBytes), maxMemUsage_);
    CHECK_LE(minFreeMem_, sysParams.totalMemBytes);

    // Max TCP mem and min free TCP mem ratios must have values in the range
    // of [0-1] inclusive and 1.0 minus min TCP mem ration must be greater than
    // or equal to max TCP mem ratio.
    CHECK_GE(maxTcpMemUsage_, 0.0);
    CHECK_LE(maxTcpMemUsage_, 1.0);
    CHECK_GE(1.0 - minFreeTcpMemPct_, maxTcpMemUsage_);
    CHECK_GE(minFreeTcpMemPct_, 0.0);
    CHECK_LE(minFreeTcpMemPct_, 1.0);

    // Active connetions obviously must be less than or equal to max
    // connections.
    CHECK(maxActiveConnections_ <= maxConnections_);
  }
}

} // namespace wangle

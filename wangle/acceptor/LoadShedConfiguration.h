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
#pragma once

#include <chrono>
#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <glog/logging.h>
#include <list>
#include <set>
#include <string>

#include <wangle/acceptor/NetworkAddress.h>

namespace wangle {

/**
 * Class that holds an LoadShed configuration for a service
 */
class LoadShedConfiguration {
 public:

  // Comparison function for SocketAddress that disregards the port
  struct AddressOnlyCompare {
    bool operator()(
     const folly::SocketAddress& addr1,
     const folly::SocketAddress& addr2) const {
      return addr1.getIPAddress() < addr2.getIPAddress();
    }
  };

  typedef std::set<folly::SocketAddress, AddressOnlyCompare> AddressSet;
  typedef std::set<NetworkAddress> NetworkSet;

  LoadShedConfiguration() = default;

  ~LoadShedConfiguration() = default;

  void addWhitelistAddr(folly::StringPiece);

  /**
   * Set/get the set of IPs that should be whitelisted through even when we're
   * trying to shed load.
   */
  void setWhitelistAddrs(const AddressSet& addrs) { whitelistAddrs_ = addrs; }
  const AddressSet& getWhitelistAddrs() const { return whitelistAddrs_; }

  /**
   * Set/get the set of networks that should be whitelisted through even
   * when we're trying to shed load.
   */
  void setWhitelistNetworks(const NetworkSet& networks) {
    whitelistNetworks_ = networks;
  }
  const NetworkSet& getWhitelistNetworks() const { return whitelistNetworks_; }

  /**
   * Set/get the maximum number of downstream connections across all VIPs.
   */
  void setMaxConnections(uint64_t maxConns) { maxConnections_ = maxConns; }
  uint64_t getMaxConnections() const { return maxConnections_; }

  /**
   * Set/get the maximum number of active downstream connections
   * across all VIPs.
   */
  void setMaxActiveConnections(uint64_t maxActiveConns) {
    maxActiveConnections_ = maxActiveConns;
  }
  uint64_t getMaxActiveConnections() const { return maxActiveConnections_; }

  /**
   * Set/get the acceptor queue size which can be used to pause accepting new
   * client connections.
   */
  void setAcceptPauseOnAcceptorQueueSize(
      const uint64_t acceptPauseOnAcceptorQueueSize) {
    acceptPauseOnAcceptorQueueSize_ = acceptPauseOnAcceptorQueueSize;
  }
  uint64_t getAcceptPauseOnAcceptorQueueSize() const {
    return acceptPauseOnAcceptorQueueSize_;
  }

  /**
   * Set/get the acceptor queue size which can be used to resume accepting new
   * client connections if accepting is paused.
   */
  void setAcceptResumeOnAcceptorQueueSize(
      const uint64_t acceptResumeOnAcceptorQueueSize) {
    acceptResumeOnAcceptorQueueSize_ = acceptResumeOnAcceptorQueueSize;
  }
  uint64_t getAcceptResumeOnAcceptorQueueSize() const {
    return acceptResumeOnAcceptorQueueSize_;
  }

  /**
   * Set/get the maximum memory usage.
   * Regarded as a soft limit; variable amount of new conn shedding should
   * occur when above this limit.
   */
  void setMaxMemUsage(double max) {
    CHECK(max >= 0);
    CHECK(max <= 1);
    maxMemUsage_ = max;
  }
  double getMaxMemUsage() const { return maxMemUsage_; }

  /**
   * Set/get the maximum cpu usage.
   * Regarded as a soft limit; variable amount of new conn shedding should
   * occur when above this limit.
   */
  void setMaxCpuUsage(double max) {
    CHECK(max >= 0);
    CHECK(max <= 1);
    maxCpuUsage_ = max;
  }
  double getMaxCpuUsage() const { return maxCpuUsage_; }

  /**
   * Set/get the minimum cpu idle.
   * Regarded as a hard limit; every new conn should shed when above this limit
   * when normalized.
   */
  void setMinCpuIdle(double min) {
    CHECK(min >= 0);
    CHECK(min <= 1);
    minCpuIdle_ = min;
  }
  double getMinCpuIdle() const { return minCpuIdle_; }

  /**
   * Set/get the number of most utilized cpu cores to use when comparing
   * against cpu limits; a value of 0 or a value that equals the total number
   * of cores on the executing system implies that mean CPU should be used.
   * This field exists to more meaningfully handle uneven load distributions
   * that can occur if for example network card interrupt affinity is set
   * such that only X of Y cores are utilized for network packet processing.
   */
  void setLogicalCpuCoreQuorum(uint64_t quorum) {
    logicalCpuCoreQuorum_ = quorum;
  }
  uint64_t getLogicalCpuCoreQuorum() const {
    return logicalCpuCoreQuorum_;
  }

  /**
   * Set/get the CPU usage exceed window size
   */
  void setCpuUsageExceedWindowSize(const uint64_t size) {
    cpuUsageExceedWindowSize_ = size;
  }

  uint64_t getCpuUsageExceedWindowSize() const {
    return cpuUsageExceedWindowSize_;
  }

  /**
   * Set/get the minium actual free memory on the system.
   * Regarded as a hard limit; every new conn should shed when above this limit
   * when normalized.
   */
  void setMinFreeMem(uint64_t min) {
    minFreeMem_ = min;
  }
  uint64_t getMinFreeMem() const {
    return minFreeMem_;
  }

  void setLoadUpdatePeriod(std::chrono::milliseconds period) {
    period_ = period;
  }
  std::chrono::milliseconds getLoadUpdatePeriod() const { return period_; }

  void setMaxTcpMemUsage(double max) {
    CHECK_GE(max, 0.0);
    CHECK_LE(max, 1.0);
    maxTcpMemUsage_ = max;
  }
  double getMaxTcpMemUsage() const {
    return maxTcpMemUsage_;
  }

  void setMinFreeTcpMemPct(double min) {
    CHECK_GE(min, 0.0);
    CHECK_LE(min, 1.0);
    minFreeTcpMemPct_ = min;
  }
  double getMinFreeTcpMemPct() const {
    return minFreeTcpMemPct_;
  }

  void setLoadSheddingEnabled(bool enabled) {
    loadSheddingEnabled_ = enabled;
  }

  bool getLoadSheddingEnabled() const {
    return loadSheddingEnabled_;
  }

  bool isWhitelisted(const folly::SocketAddress& addr) const;

  /**
   * Performs a series of CHECKs to ensure the underlying configuration is
   * sane.
   * For example the following must be true: (1.0 - minCpuIdle_) >= maxCpuUsage_
   * Note: totalMemBytes represents the total system memory to be used when
   * normalizing minFreeMem_ and killMinFreeMemBytes_ in order for the
   * associated comparisons.
   */
  struct SysParams {
    uint64_t numLogicalCpuCores{0};
    uint64_t totalMemBytes{0};
  };
  void checkIsSane(const SysParams& sysParams) const;

 private:

  AddressSet whitelistAddrs_;
  NetworkSet whitelistNetworks_;
  uint64_t maxConnections_{0};
  uint64_t maxActiveConnections_{0};
  uint64_t acceptPauseOnAcceptorQueueSize_{0};
  uint64_t acceptResumeOnAcceptorQueueSize_{0};
  uint64_t minFreeMem_{0};
  double maxMemUsage_{1.0};
  double maxCpuUsage_{1.0};
  double minCpuIdle_{0.0};
  uint64_t logicalCpuCoreQuorum_{0};
  uint64_t cpuUsageExceedWindowSize_{0};
  double maxTcpMemUsage_{1.0};
  double minFreeTcpMemPct_{0.0};
  std::chrono::milliseconds period_;
  bool loadSheddingEnabled_{true};
};

} // namespace wangle

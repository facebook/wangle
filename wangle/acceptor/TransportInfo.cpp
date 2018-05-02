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
#include <wangle/acceptor/TransportInfo.h>

#include <sys/types.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/portability/Sockets.h>

using std::chrono::microseconds;
using std::map;
using std::string;

namespace wangle {

bool TransportInfo::initWithSocket(const folly::AsyncSocket* sock) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  if (!TransportInfo::readTcpInfo(&tcpinfo, sock)) {
    tcpinfoErrno = errno;
    return false;
  }
#ifdef __APPLE__
  rtt = microseconds(tcpinfo.tcpi_srtt * 1000);
  rtt_var = tcpinfo.tcpi_rttvar * 1000;
  rto = tcpinfo.tcpi_rto * 1000;
  rtx_tm = -1;
  mss = tcpinfo.tcpi_maxseg;
  cwndBytes = tcpinfo.tcpi_snd_cwnd;
  if (mss > 0) {
    cwnd = (cwndBytes + mss - 1) / mss;
  }
#else
  rtt = microseconds(tcpinfo.tcpi_rtt);
  rtt_var = tcpinfo.tcpi_rttvar;
  rto = tcpinfo.tcpi_rto;
  rtx_tm = tcpinfo.tcpi_retransmits;
  mss = tcpinfo.tcpi_snd_mss;
  cwnd = tcpinfo.tcpi_snd_cwnd;
  cwndBytes = cwnd * mss;
#endif // __APPLE__
  ssthresh = tcpinfo.tcpi_snd_ssthresh;
#if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 17
  rtx = tcpinfo.tcpi_total_retrans;
#else
  rtx = -1;
#endif  // __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 17
  validTcpinfo = true;
#else
  tcpinfoErrno = EINVAL;
  rtt = microseconds(-1);
  rtt_var = -1;
  rtx = -1;
  rtx_tm = -1;
  rto = -1;
  cwnd = -1;
  mss = -1;
  ssthresh = -1;
#endif
  return true;
}

int64_t TransportInfo::readRTT(const folly::AsyncSocket* sock) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  tcp_info tcpinfo;
  if (!TransportInfo::readTcpInfo(&tcpinfo, sock)) {
    return -1;
  }
#endif
#if defined(__linux__) || defined(__FreeBSD__)
  return tcpinfo.tcpi_rtt;
#elif defined(__APPLE__)
  return tcpinfo.tcpi_srtt;
#else
  return -1;
#endif
}

#ifdef __APPLE__
#define TCP_INFO TCP_CONNECTION_INFO
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
bool TransportInfo::readTcpInfo(tcp_info* tcpinfo,
                                const folly::AsyncSocket* sock) {
  socklen_t len = sizeof(tcp_info);
  if (!sock) {
    return false;
  }
  if (getsockopt(sock->getFd(), IPPROTO_TCP,
                 TCP_INFO, (void*) tcpinfo, &len) < 0) {
    VLOG(4) << "Error calling getsockopt(): " << strerror(errno);
    return false;
  }
  return true;
}
#endif

} // namespace wangle

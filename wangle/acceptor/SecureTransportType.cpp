/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/SecureTransportType.h>

namespace wangle {

std::string getSecureTransportName(const SecureTransportType& type) {
  switch (type) {
  case SecureTransportType::TLS:
    return "TLS";
  case SecureTransportType::ZERO:
    return "Zero";
  default:
    return "";
  }
}

}

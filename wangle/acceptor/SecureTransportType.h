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

/**
 * An enum representing different kinds
 * of secure transports we can negotiate.
 */
enum class SecureTransportType {
  NONE, // Transport is not secure.
  TLS,  // Transport is based on TLS
  ZERO, // Transport is based on zero protocol
};

}

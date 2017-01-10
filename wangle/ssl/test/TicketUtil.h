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

#include <folly/Range.h>

constexpr folly::StringPiece validTicketData =
    R"JSON({
    "new": [
      "123",
      "234"
    ],
    "current": [
      "123"
    ]
  })JSON";

// Has invalid strict json structure.
constexpr folly::StringPiece invalidTicketData =
    R"JSON({
    'new': [
      "123',
      "234"
    ],
    "current": [
      "123"
    ],
  })JSON";

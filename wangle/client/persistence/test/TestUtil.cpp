/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/client/persistence/test/TestUtil.h>

#include <folly/experimental/TestUtil.h>

namespace wangle {

std::string getPersistentCacheFilename() {
  folly::test::TemporaryFile file("fbtls");
  return file.path().string();
}

}

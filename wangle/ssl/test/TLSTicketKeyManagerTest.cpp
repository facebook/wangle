/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>
#include <wangle/ssl/TLSTicketKeyManager.h>
#include <wangle/ssl/SSLStats.h>

TEST(TLSTicketKeyManager, TestSetGetTLSTicketKeySeeds) {
  std::vector<std::string> origOld = {"67"};
  std::vector<std::string> origCurr = {"68"};
  std::vector<std::string> origNext = {"69"};

  folly::SSLContext ctx;
  wangle::TLSTicketKeyManager manager(&ctx, nullptr);

  manager.setTLSTicketKeySeeds(origOld, origCurr, origNext);
  std::vector<std::string> old;
  std::vector<std::string> curr;
  std::vector<std::string> next;
  manager.getTLSTicketKeySeeds(old, curr, next);
  ASSERT_EQ(origOld, old);
  ASSERT_EQ(origCurr, curr);
  ASSERT_EQ(origNext, next);
}

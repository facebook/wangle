/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <boost/filesystem.hpp>
#include <folly/portability/GTest.h>
#include <folly/Baton.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Range.h>
#include <wangle/ssl/TLSCredProcessor.h>
#include <wangle/ssl/test/TicketUtil.h>

using namespace testing;
using namespace folly;
using namespace wangle;

namespace fs = boost::filesystem;

class ProcessTicketTest : public testing::Test {
 public:
  void SetUp() override {
    char ticketTemp[] = {"/tmp/ticketFile-XXXXXX"};
    File(mkstemp(ticketTemp), true);
    ticketFile = ticketTemp;
    char certTemp[] = {"/tmp/certFile-XXXXXX"};
    File(mkstemp(certTemp), true);
    certFile = certTemp;
  }

  void TearDown() override {
    remove(ticketFile.c_str());
    remove(certFile.c_str());
  }

  std::string ticketFile;
  std::string certFile;
};

void expectValidData(folly::Optional<wangle::TLSTicketKeySeeds> seeds) {
  ASSERT_TRUE(seeds);
  ASSERT_EQ(2, seeds->newSeeds.size());
  ASSERT_EQ(1, seeds->currentSeeds.size());
  ASSERT_EQ(0, seeds->oldSeeds.size());
  ASSERT_EQ("123", seeds->newSeeds[0]);
  ASSERT_EQ("234", seeds->newSeeds[1]);
}

TEST_F(ProcessTicketTest, ParseTicketFile) {
  CHECK(writeFile(validTicketData, ticketFile.c_str()));
  auto seeds = TLSCredProcessor::processTLSTickets(ticketFile);
  expectValidData(seeds);
}

TEST_F(ProcessTicketTest, ParseInvalidFile) {
  CHECK(writeFile(invalidTicketData, ticketFile.c_str()));
  auto seeds = TLSCredProcessor::processTLSTickets(ticketFile);
  ASSERT_FALSE(seeds);
}

void updateModifiedTime(const std::string& fileName) {
  auto previous = fs::last_write_time(fileName);
  auto newTime = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::from_time_t(previous) +
      std::chrono::seconds(10));
  fs::last_write_time(fileName, newTime);
}

TEST_F(ProcessTicketTest, TestUpdateTicketFile) {
  Baton<> ticketBaton;
  Baton<> certBaton;
  TLSCredProcessor processor(ticketFile, certFile);
  bool ticketUpdated = false;
  bool certUpdated = false;
  processor.addTicketCallback([&](TLSTicketKeySeeds) {
    ticketUpdated = true;
    ticketBaton.post();
  });
  processor.addCertCallback([&]() {
    certUpdated = true;
    certBaton.post();
  });
  CHECK(writeFile(validTicketData, ticketFile.c_str()));
  updateModifiedTime(ticketFile);
  ticketBaton.timed_wait(std::chrono::seconds(30));
  ASSERT_TRUE(ticketUpdated);
  ASSERT_FALSE(certUpdated);
  ticketUpdated = false;
  CHECK(writeFile(validTicketData, certFile.c_str()));
  updateModifiedTime(certFile);
  certBaton.timed_wait(std::chrono::seconds(30));
  ASSERT_TRUE(certUpdated);
  ASSERT_FALSE(ticketUpdated);
}

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
#include <boost/filesystem.hpp>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
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

TEST_F(ProcessTicketTest, handleAbsentFile) {
  auto seeds = TLSCredProcessor::processTLSTickets("/path/does/not/exist");
  ASSERT_FALSE(seeds);
}

void updateModifiedTime(const std::string& fileName, int elapsed) {
  auto previous = fs::last_write_time(fileName);
  auto newTime = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::from_time_t(previous) +
      std::chrono::seconds(elapsed));
  fs::last_write_time(fileName, newTime);
}

TEST_F(ProcessTicketTest, TestUpdateTicketFile) {
  Baton<> ticketBaton;
  Baton<> certBaton;
  TLSCredProcessor processor;
  processor.setTicketPathToWatch(ticketFile);
  processor.setCertPathsToWatch({certFile});
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
  updateModifiedTime(ticketFile,10);
  EXPECT_TRUE(ticketBaton.try_wait_for(std::chrono::seconds(30)));
  ASSERT_TRUE(ticketUpdated);
  ASSERT_FALSE(certUpdated);
  ticketUpdated = false;
  CHECK(writeFile(validTicketData, certFile.c_str()));
  updateModifiedTime(certFile,10);
  EXPECT_TRUE(certBaton.try_wait_for(std::chrono::seconds(30)));
  ASSERT_TRUE(certUpdated);
  ASSERT_FALSE(ticketUpdated);
}

TEST_F(ProcessTicketTest, TestMultipleCerts) {
  Baton<> certBaton;
  TLSCredProcessor processor;
  processor.setCertPathsToWatch({certFile, ticketFile});
  processor.addCertCallback([&]() {
    certBaton.post();
  });
  CHECK(writeFile(validTicketData, ticketFile.c_str()));
  updateModifiedTime(ticketFile,10);
  EXPECT_TRUE(certBaton.try_wait_for(std::chrono::seconds(30)));
  certBaton.reset();
  CHECK(writeFile(validTicketData, certFile.c_str()));
  updateModifiedTime(certFile,10);
  EXPECT_TRUE(certBaton.try_wait_for(std::chrono::seconds(30)));
}

TEST_F(ProcessTicketTest, TestSetPullInterval) {
  Baton<> ticketBaton;
  Baton<> certBaton;
  TLSCredProcessor processor;
  processor.setTicketPathToWatch(ticketFile);
  processor.setCertPathsToWatch({certFile});
  processor.setPollInterval(std::chrono::seconds(3));
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
  updateModifiedTime(ticketFile,3);
  EXPECT_TRUE(ticketBaton.try_wait_for(std::chrono::seconds(5)));
  ASSERT_TRUE(ticketUpdated);
  ASSERT_FALSE(certUpdated);
  ticketUpdated = false;
  CHECK(writeFile(validTicketData, certFile.c_str()));
  updateModifiedTime(certFile,3);
  EXPECT_TRUE(certBaton.try_wait_for(std::chrono::seconds(5)));
  ASSERT_TRUE(certUpdated);
  ASSERT_FALSE(ticketUpdated);
}

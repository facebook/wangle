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
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <boost/filesystem.hpp>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/experimental/TestUtil.h>
#include <wangle/util/FilePoller.h>

using namespace testing;
using namespace folly;
using namespace wangle;
using namespace folly::test;

class FilePollerTest : public testing::Test {
 public:
  void createFile() { File(tmpFile, O_CREAT); }

  TemporaryDirectory tmpDir;
  fs::path tmpFilePath{tmpDir.path() / "file-poller"};
  std::string tmpFile{tmpFilePath.string()};
};

void updateModifiedTime(const std::string& fileName, bool forward = true) {
  auto previous = std::chrono::system_clock::from_time_t(
      boost::filesystem::last_write_time(fileName));
  auto diff = std::chrono::seconds(10);
  std::chrono::system_clock::time_point newTime;
  if (forward) {
    newTime = previous + diff;
  } else {
    newTime = previous - diff;
  }

  time_t newTimet = std::chrono::system_clock::to_time_t(newTime);
  boost::filesystem::last_write_time(fileName, newTimet);
}

TEST_F(FilePollerTest, TestUpdateFile) {
  createFile();
  Baton<> baton;
  bool updated = false;
  FilePoller poller(std::chrono::milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  updateModifiedTime(tmpFile);
  ASSERT_TRUE(baton.try_wait_for(std::chrono::seconds(5)));
  ASSERT_TRUE(updated);
}

TEST_F(FilePollerTest, TestUpdateFileBackwards) {
  createFile();
  Baton<> baton;
  bool updated = false;
  FilePoller poller(std::chrono::milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  updateModifiedTime(tmpFile, false);
  ASSERT_TRUE(baton.try_wait_for(std::chrono::seconds(5)));
  ASSERT_TRUE(updated);
}

TEST_F(FilePollerTest, TestCreateFile) {
  Baton<> baton;
  bool updated = false;
  createFile();
  remove(tmpFile.c_str());
  FilePoller poller(std::chrono::milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  File(creat(tmpFile.c_str(), O_RDONLY));
  ASSERT_TRUE(baton.try_wait_for(std::chrono::seconds(5)));
  ASSERT_TRUE(updated);
}

TEST_F(FilePollerTest, TestDeleteFile) {
  Baton<> baton;
  bool updated = false;
  createFile();
  FilePoller poller(std::chrono::milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  remove(tmpFile.c_str());
  ASSERT_FALSE(baton.try_wait_for(std::chrono::seconds(1)));
  ASSERT_FALSE(updated);
}

struct UpdateSyncState {
  std::mutex m;
  std::condition_variable cv;
  bool updated{false};

  void updateTriggered() {
    std::unique_lock<std::mutex> lk(m);
    updated = true;
    cv.notify_one();
  }

  void waitForUpdate(bool expect = true) {
    std::unique_lock<std::mutex> lk(m);
    cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return updated; });
    ASSERT_EQ(updated, expect);
    updated = false;
  }
};

class TestFile {
 public:
  TestFile(bool exists, time_t mTime) : exists_(exists), modTime_(mTime) {}

  void update(bool e, time_t t) {
    std::unique_lock<std::mutex> lk(m);
    exists_ = e;
    modTime_ = t;
  }

  FilePoller::FileModificationData toFileModData() {
    std::unique_lock<std::mutex> lk(m);
    return FilePoller::FileModificationData(exists_, modTime_);
  }

  const std::string name{"fakeFile"};
 private:
  bool exists_{false};
  time_t modTime_{0};
  std::mutex m;

};

class NoDiskPoller : public FilePoller {
 public:
  explicit NoDiskPoller(TestFile& testFile)
    : FilePoller(std::chrono::milliseconds(10)), testFile_(testFile) {}

 protected:
  FilePoller::FileModificationData
  getFileModData(const std::string& path) noexcept override {
    EXPECT_EQ(path, testFile_.name);
    return testFile_.toFileModData();
  }

 private:
  TestFile& testFile_;
};

struct PollerWithState {
  explicit PollerWithState(TestFile& testFile) {
    poller = std::make_unique<NoDiskPoller>(testFile);
    poller->addFileToTrack(testFile.name, [&] {
      state.updateTriggered();
    });
  }

  void waitForUpdate(bool expect = true) {
    ASSERT_NO_FATAL_FAILURE(state.waitForUpdate(expect));
  }

  std::unique_ptr<FilePoller> poller;
  UpdateSyncState state;
};

TEST_F(FilePollerTest, TestTwoUpdatesAndDelete) {
  TestFile testFile(true, 1);
  PollerWithState poller(testFile);

  testFile.update(true, 2);
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate());

  testFile.update(true, 3);
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate());

  testFile.update(false, 0);
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate(false));
}

TEST_F(FilePollerTest, TestFileCreatedLate) {
  TestFile testFile(false, 0); // not created yet
  PollerWithState poller(testFile);
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate(false));

  testFile.update(true, 1);
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate());
}

TEST_F(FilePollerTest, TestMultiplePollers) {
  TestFile testFile(true, 1);
  PollerWithState p1(testFile);
  PollerWithState p2(testFile);

  testFile.update(true, 2);
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
  ASSERT_NO_FATAL_FAILURE(p2.waitForUpdate());

  testFile.update(true, 1);
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
  ASSERT_NO_FATAL_FAILURE(p2.waitForUpdate());

  // clear one of the pollers and make sure the other is still
  // getting them
  p2.poller.reset();
  testFile.update(true, 3);
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
  ASSERT_NO_FATAL_FAILURE(p2.waitForUpdate(false));
}

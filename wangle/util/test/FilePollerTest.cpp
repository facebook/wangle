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

#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Singleton.h>
#include <folly/experimental/TestUtil.h>
#include <folly/portability/GTest.h>
#include <folly/portability/SysStat.h>
#include <folly/synchronization/Baton.h>
#include <wangle/util/FilePoller.h>

using namespace testing;
using namespace folly;
using namespace wangle;
using namespace folly::test;
using namespace std::chrono;

class FilePollerTest : public testing::Test {
 public:
  void createFile() { File(tmpFile, O_CREAT); }

  TemporaryDirectory tmpDir;
  fs::path tmpFilePath{tmpDir.path() / "file-poller"};
  std::string tmpFile{tmpFilePath.string()};
};

void updateModifiedTime(
    const std::string& path,
    bool forward = true,
    system_clock::time_point timeDiff = system_clock::time_point(seconds(10))) {
  struct stat64 currentFileStat;
  std::array<struct timespec, 2> newTimes;

  if (stat64(path.c_str(), &currentFileStat) < 0) {
    throw std::runtime_error("Failed to stat file: " + path);
  }

  newTimes[0] = currentFileStat.st_atim;
  newTimes[1] = currentFileStat.st_mtim;
  auto secVal = time_point_cast<seconds>(timeDiff).time_since_epoch().count();
  auto nsecVal =
      (time_point_cast<nanoseconds>(timeDiff).time_since_epoch() % (long)1e9)
          .count();
  if (forward) {
    newTimes[1].tv_sec += secVal;
    newTimes[1].tv_nsec += nsecVal;
  } else {
    newTimes[1].tv_sec -= secVal;
    newTimes[1].tv_nsec -= nsecVal;
  }

  if (utimensat(AT_FDCWD, path.c_str(), newTimes.data(), 0) < 0) {
    throw std::runtime_error("Failed to set time for file: " + path);
  }
}

TEST_F(FilePollerTest, TestUpdateFile) {
  createFile();
  Baton<> baton;
  bool updated = false;
  FilePoller poller(milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  updateModifiedTime(tmpFile);
  ASSERT_TRUE(baton.try_wait_for(seconds(5)));
  ASSERT_TRUE(updated);
}

TEST_F(FilePollerTest, TestUpdateFileSubSecond) {
  createFile();
  Baton<> baton;
  bool updated = false;
  FilePoller poller(milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  updateModifiedTime(tmpFile, true, system_clock::time_point(milliseconds(10)));
  ASSERT_TRUE(baton.try_wait_for(seconds(5)));
  ASSERT_TRUE(updated);
}

TEST_F(FilePollerTest, TestUpdateFileBackwards) {
  createFile();
  Baton<> baton;
  bool updated = false;
  FilePoller poller(milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  updateModifiedTime(tmpFile, false);
  ASSERT_TRUE(baton.try_wait_for(seconds(5)));
  ASSERT_TRUE(updated);
}

TEST_F(FilePollerTest, TestCreateFile) {
  Baton<> baton;
  bool updated = false;
  createFile();
  remove(tmpFile.c_str());
  FilePoller poller(milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  File(creat(tmpFile.c_str(), O_RDONLY));
  ASSERT_TRUE(baton.try_wait_for(seconds(5)));
  ASSERT_TRUE(updated);
}

TEST_F(FilePollerTest, TestDeleteFile) {
  Baton<> baton;
  bool updated = false;
  createFile();
  FilePoller poller(milliseconds(1));
  poller.addFileToTrack(tmpFile, [&]() {
    updated = true;
    baton.post();
  });
  remove(tmpFile.c_str());
  ASSERT_FALSE(baton.try_wait_for(seconds(1)));
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
    cv.wait_for(lk, milliseconds(100), [&] { return updated; });
    ASSERT_EQ(updated, expect);
    updated = false;
  }
};

class TestFile {
 public:
  TestFile(bool exists, system_clock::time_point modTime)
      : exists_(exists), modTime_(modTime) {}

  void update(bool e, system_clock::time_point t) {
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
  system_clock::time_point modTime_;
  std::mutex m;

};

class NoDiskPoller : public FilePoller {
 public:
  explicit NoDiskPoller(TestFile& testFile)
      : FilePoller(milliseconds(10)), testFile_(testFile) {}

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
  TestFile testFile(true, system_clock::time_point(seconds(1)));
  PollerWithState poller(testFile);

  testFile.update(true, system_clock::time_point(seconds(2)));
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate());

  testFile.update(true, system_clock::time_point(seconds(3)));
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate());

  testFile.update(false, system_clock::time_point(seconds(0)));
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate(false));
}

TEST_F(FilePollerTest, TestFileCreatedLate) {
  TestFile testFile(
      false,
      system_clock::time_point(seconds(0))); // not created yet
  PollerWithState poller(testFile);
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate(false));

  testFile.update(true, system_clock::time_point(seconds(1)));
  ASSERT_NO_FATAL_FAILURE(poller.waitForUpdate());
}

TEST_F(FilePollerTest, TestMultiplePollers) {
  TestFile testFile(true, system_clock::time_point(seconds(1)));
  PollerWithState p1(testFile);
  PollerWithState p2(testFile);

  testFile.update(true, system_clock::time_point(seconds(2)));
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
  ASSERT_NO_FATAL_FAILURE(p2.waitForUpdate());

  testFile.update(true, system_clock::time_point(seconds(1)));
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
  ASSERT_NO_FATAL_FAILURE(p2.waitForUpdate());

  // clear one of the pollers and make sure the other is still
  // getting them
  p2.poller.reset();
  testFile.update(true, system_clock::time_point(seconds(3)));
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
  ASSERT_NO_FATAL_FAILURE(p2.waitForUpdate(false));
}

TEST(FilePoller, TestFork) {
  TestFile testFile(true, system_clock::time_point(seconds(1)));
  PollerWithState p1(testFile);
  testFile.update(true, system_clock::time_point(seconds(2)));
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
  // nuke singleton
  folly::SingletonVault::singleton()->destroyInstances();
  testFile.update(true, system_clock::time_point(seconds(3)));
  ASSERT_NO_FATAL_FAILURE(p1.waitForUpdate());
}

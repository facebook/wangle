/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/util/FilePoller.h>

#include <atomic>
#include <sys/stat.h>

#include <folly/Conv.h>
#include <folly/Memory.h>
#include <folly/Singleton.h>

using namespace folly;

namespace wangle {

namespace {
class PollerContext {
 public:
  PollerContext() : nextPollerId(1) {
    scheduler.setThreadName("file-poller");
    scheduler.start();
  }

  folly::FunctionScheduler& getScheduler() {
    return scheduler;
  }

  uint64_t getNextId() {
    return nextPollerId++;
  }

 private:
  folly::FunctionScheduler scheduler;
  std::atomic<uint64_t> nextPollerId;
};

folly::Singleton<PollerContext> contextSingleton([] {
  return new PollerContext();
});
}

folly::ThreadLocal<bool> FilePoller::ThreadProtector::polling_([] {
  return new bool(false);
});

constexpr std::chrono::milliseconds FilePoller::kDefaultPollInterval;

FilePoller::FilePoller(std::chrono::milliseconds pollInterval) {
  init(pollInterval);
}

FilePoller::~FilePoller() { stop(); }

void FilePoller::init(std::chrono::milliseconds pollInterval) {
  auto context = contextSingleton.try_get();
  if (!context) {
    LOG(ERROR) << "Poller context requested after destruction.";
    return;
  }
  pollerId_ = context->getNextId();
  context->getScheduler().addFunction(
      [this] { this->checkFiles(); },
      pollInterval,
      folly::to<std::string>(pollerId_));
}

void FilePoller::stop() {
  auto context = contextSingleton.try_get();
  if (!context) {
    // already destroyed/stopped;
    return;
  }
  context->getScheduler().cancelFunction(
      folly::to<std::string>(pollerId_));
}

void FilePoller::checkFiles() noexcept {
  std::lock_guard<std::mutex> lg(filesMutex_);
  ThreadProtector tp;
  for (auto& fData : fileDatum_) {
    auto modData = getFileModData(fData.first);
    auto& fileData = fData.second;
    if (fileData.condition(fileData.modData, modData) && fileData.yCob) {
      fileData.yCob();
    } else if (fileData.nCob) {
      fileData.nCob();
    }
    fileData.modData = modData;
  }
}

void FilePoller::initFileData(
    const std::string& fName,
    FileData& fData) noexcept {
  auto modData = getFileModData(fName);
  fData.modData.exists = modData.exists;
  fData.modData.modTime = modData.modTime;
}

void FilePoller::addFileToTrack(
    const std::string& fileName,
    Cob yCob,
    Cob nCob,
    Condition condition) {
  if (ThreadProtector::inPollerThread()) {
    LOG(ERROR) << "Adding files from a callback is disallowed";
    return;
  }
  std::lock_guard<std::mutex> lg(filesMutex_);
  fileDatum_[fileName] = FileData(yCob, nCob, condition);
  initFileData(fileName, fileDatum_[fileName]);
}

void FilePoller::removeFileToTrack(const std::string& fileName) {
  if (ThreadProtector::inPollerThread()) {
    LOG(ERROR) << "Adding files from a callback is disallowed";
    return;
  }
  std::lock_guard<std::mutex> lg(filesMutex_);
  fileDatum_.erase(fileName);
}

FilePoller::FileModificationData FilePoller::getFileModData(
    const std::string& path) noexcept {
  struct stat info;
  int ret = stat(path.c_str(), &info);
  if (ret != 0) {
    return FileModificationData{false, 0};
  }
  return FileModificationData{true, info.st_mtime};
}
}

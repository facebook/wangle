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

#include <sys/stat.h>

using namespace folly;

namespace wangle {

folly::ThreadLocal<bool> FilePoller::ThreadProtector::polling_([] {
  return new bool(false);
});

constexpr std::chrono::milliseconds FilePoller::kDefaultPollInterval;

FilePoller::FilePoller(std::chrono::milliseconds pollInterval) {
  init(pollInterval);
}

FilePoller::~FilePoller() { scheduler_.shutdown(); }

void FilePoller::init(std::chrono::milliseconds pollInterval) {
  scheduler_.setThreadName("file-poller");
  scheduler_.addFunction(
      [this] { this->checkFiles(); }, pollInterval, "file-poller");
  start();
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

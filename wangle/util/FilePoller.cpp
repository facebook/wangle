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
  context->getScheduler().cancelFunctionAndWait(
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
  if (fileName.empty()) {
    // ignore empty file paths
    return;
  }
  if (ThreadProtector::inPollerThread()) {
    LOG(ERROR) << "Adding files from a callback is disallowed";
    return;
  }
  std::lock_guard<std::mutex> lg(filesMutex_);
  fileDatum_[fileName] = FileData(yCob, nCob, condition);
  initFileData(fileName, fileDatum_[fileName]);
}

void FilePoller::removeFileToTrack(const std::string& fileName) {
  if (fileName.empty()) {
    // ignore
    return;
  }
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

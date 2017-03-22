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

#include <chrono>
#include <functional>
#include <memory>

#include <folly/SharedMutex.h>
#include <folly/ThreadLocal.h>
#include <folly/experimental/FunctionScheduler.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/ScopedEventBaseThread.h>

namespace wangle {

/**
 * Polls for updates in the files. This poller uses
 * modified times to track changes in the files,
 * so it is the responsibility of the caller to check
 * whether the contents have actually changed.
 * It also assumes that when the file is modified, the
 * modfied time changes. This is a reasonable
 * assumption to make, so it should not be used in
 * situations where files may be modified without
 * changing the modified time.
 */
class FilePoller {
 public:
  struct FileModificationData {
    FileModificationData() {}
    FileModificationData(bool fileExists, time_t modificationTime)
        : exists(fileExists), modTime(modificationTime) {}
    bool exists{false};
    time_t modTime{0};
  };
  using Cob = std::function<void()>;
  // First arg is info about previous modification of a file,
  // Second arg is info about last modification of a file
  using Condition = std::function<
      bool(const FileModificationData&, const FileModificationData&)>;

  explicit FilePoller(std::chrono::milliseconds pollInterval =
                      kDefaultPollInterval);

  virtual ~FilePoller();

  FilePoller(const FilePoller& other) = delete;
  FilePoller& operator=(const FilePoller& other) = delete;

  FilePoller(FilePoller&& other) = delete;
  FilePoller&& operator=(FilePoller&& other) = delete;

  // This is threadsafe
  // yCob will be called if condition is met,
  // nCob is called if condition is not met.
  void addFileToTrack(
      const std::string& fileName,
      Cob yCob,
      Cob nCob = nullptr,
      Condition condition = fileTouchedCondInternal);

  void removeFileToTrack(const std::string& fileName);

  void stop();

  static Condition fileTouchedWithinCond(std::chrono::seconds expireTime) {
    return std::bind(
        fileTouchedWithinCondInternal,
        std::placeholders::_1,
        std::placeholders::_2,
        expireTime);
  }

  static Condition doAlwaysCond() {
    return doAlwaysCondInternal;
  }

  static Condition fileTouchedCond() {
    return fileTouchedCondInternal;
  }

 protected:
  virtual FileModificationData getFileModData(const std::string& path) noexcept;

 private:
  static constexpr std::chrono::milliseconds kDefaultPollInterval =
        std::chrono::milliseconds(10000);

  struct FileData {
    FileData(Cob yesCob, Cob noCob, Condition cond)
        : yCob(yesCob), nCob(noCob), condition(cond) {}
    FileData() {}

    Cob yCob{nullptr};
    Cob nCob{nullptr};
    Condition condition{doAlwaysCondInternal};
    FileModificationData modData;
  };
  using FileDatas = std::unordered_map<std::string, FileData>;

  static bool fileTouchedWithinCondInternal(
      const FilePoller::FileModificationData&,
      const FilePoller::FileModificationData& fModData,
      std::chrono::seconds expireTime) {
    return fModData.exists &&
        (time(nullptr) < fModData.modTime + expireTime.count());
  }

  static bool doAlwaysCondInternal(
      const FileModificationData&,
      const FileModificationData&) {
    return true;
  }

  static bool fileTouchedCondInternal(
      const FileModificationData& oldModData,
      const FileModificationData& modData) {
    const bool fileStillExists = oldModData.exists && modData.exists;
    const bool fileTouched = modData.modTime != oldModData.modTime;
    const bool fileCreated = !oldModData.exists && modData.exists;
    return (fileStillExists && fileTouched) || fileCreated;
  }

  // Grabs a read lock
  void checkFiles() noexcept;
  void initFileData(const std::string& fName, FileData& fData) noexcept;
  void init(std::chrono::milliseconds pollInterval);

  FileDatas fileDatum_;

  // This protects fileDatum_.
  std::mutex filesMutex_;

  uint64_t pollerId_{0};

  // Used to disallow locking calls from callbacks.
  class ThreadProtector {
   public:
    ThreadProtector() {
      *polling_ = true;
    }
    ~ThreadProtector() {
      *polling_ = false;
    }
    static bool inPollerThread() {
      return *polling_;
    }
    static folly::ThreadLocal<bool> polling_;
  };
};
}

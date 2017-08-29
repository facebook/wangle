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
#pragma once


#include <folly/FileUtil.h>
#include <folly/portability/Unistd.h>
#include <folly/json.h>

namespace wangle {

template<typename K, typename V>
class FilePersistenceLayer : public CachePersistence<K, V> {
 public:
  explicit FilePersistenceLayer(const std::string& file) : file_(file) {}
  ~FilePersistenceLayer() override {}

  bool persist(const folly::dynamic& arrayOfKvPairs) noexcept override;

  folly::Optional<folly::dynamic> load() noexcept override;

  void clear() override;

 private:
  std::string file_;
};

template<typename K, typename V>
bool FilePersistenceLayer<K, V>::persist(
  const folly::dynamic& dynObj) noexcept {
  std::string serializedCache;
  try {
    folly::json::serialization_opts opts;
    opts.allow_non_string_keys = true;
    serializedCache = folly::json::serialize(dynObj, opts);
  } catch (const std::exception& err) {
    LOG(ERROR) << "Serializing to JSON failed with error: " << err.what();
    return false;
  }
  bool persisted = false;
  const auto fd = folly::openNoInt(
    file_.c_str(),
    O_WRONLY | O_CREAT | O_TRUNC,
    S_IRUSR | S_IWUSR
  );
  if (fd == -1) {
    return false;
  }
  const auto nWritten = folly::writeFull(
    fd,
    serializedCache.data(),
    serializedCache.size()
  );
  persisted = nWritten >= 0 &&
    (static_cast<size_t>(nWritten) == serializedCache.size());
  if (!persisted) {
    LOG(ERROR) << "Failed to write to " << file_ << ":";
    if (nWritten == -1) {
      LOG(ERROR) << "write failed with errno " << errno;
    }
  }
  if (folly::fdatasyncNoInt(fd) != 0) {
    LOG(ERROR) << "Failed to sync " << file_ << ": errno " << errno;
    persisted = false;
  }
  if (folly::closeNoInt(fd) != 0) {
    LOG(ERROR) << "Failed to close " << file_ << ": errno " << errno;
    persisted = false;
  }
  return persisted;
}

template<typename K, typename V>
folly::Optional<folly::dynamic> FilePersistenceLayer<K, V>::load() noexcept {
  std::string serializedCache;
  // not being able to read the backing storage means we just
  // start with an empty cache. Failing to deserialize, or write,
  // is a real error so we report errors there.
  if (!folly::readFile(file_.c_str(), serializedCache)) {
    return folly::none;
  }

  try {
    folly::json::serialization_opts opts;
    opts.allow_non_string_keys = true;
    return folly::parseJson(serializedCache, opts);
  } catch (const std::exception& err) {
    LOG(ERROR) << "Deserialization of cache file " << file_
               << " failed with parse error: " << err.what();
  }
  return folly::none;
}

template<typename K, typename V>
void FilePersistenceLayer<K, V>::clear() {
  // This may fail but it's ok
  ::unlink(file_.c_str());
}

template<typename K, typename V, typename M>
FilePersistentCache<K, V, M>::FilePersistentCache(
  const std::string& file,
  const std::size_t cacheCapacity,
  const std::chrono::seconds& syncInterval,
  const int nSyncRetries)
    : cache_(cacheCapacity,
        std::chrono::duration_cast<std::chrono::milliseconds>(syncInterval),
        nSyncRetries,
        std::make_unique<FilePersistenceLayer<K, V>>(file)) {}
} // namespace wangle

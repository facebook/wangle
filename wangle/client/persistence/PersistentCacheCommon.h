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

#include <mutex>

namespace wangle {

/**
 * A guard that provides write and read access to a mutex type.
 */
template<typename MutexT>
struct CacheLockGuard;

// Specialize on std::mutex by providing exclusive access
template<>
struct CacheLockGuard<std::mutex> {
  using Read = std::lock_guard<std::mutex>;
  using Write = std::lock_guard<std::mutex>;
};

/**
 * A counter that represents a "version" of the data.  This is used to determine
 * if two components have been synced to the same version.
 * A valid version is 1 or higher.  A version of 0 implies no version.
 */
using CacheDataVersion = uint64_t;

}

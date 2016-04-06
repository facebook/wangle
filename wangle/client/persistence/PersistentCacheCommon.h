/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
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

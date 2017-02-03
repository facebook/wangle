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

#include <folly/Optional.h>
#include <wangle/client/persistence/PersistentCache.h>

namespace wangle {

template <typename K, typename V>
class MockPersistentCache: public PersistentCache<K, V> {
 public:
  MOCK_METHOD1_T(get, folly::Optional<V>(const K&));
  MOCK_METHOD2_T(put, void(const K&, const V&));
  MOCK_METHOD1_T(remove, bool(const K&));
  MOCK_METHOD1_T(clear, void(bool));
  MOCK_METHOD0_T(size, size_t());
};

}

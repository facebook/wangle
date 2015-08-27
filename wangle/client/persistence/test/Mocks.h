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
  MOCK_METHOD0_T(clear, void());
  MOCK_METHOD0_T(size, size_t());
};

}

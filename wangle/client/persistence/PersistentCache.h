// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <folly/Optional.h>
#include <string>

namespace wangle {

/**
 * Interface for a persistent cache that backs up the cache on
 * storage so that it can be reused. This desribes just the key
 * operations common to any cache. Loading from and syncing to
 * the storage is in the actual implementation of the class and
 * the clients should not have to worry about it.
 */
template <typename K, typename V>
class PersistentCache {
  public:
    virtual ~PersistentCache() {};

    /**
     * Get a value corresponding to a key
     * @param key string, the key to lookup
     *
     * @returns value associated with key
     */
    virtual folly::Optional<V> get(const K& key) = 0;

    /**
     * Set a value corresponding to a key
     * @param key string, the key to set
     * @param val string, the value to set
     *
     * overwrites value if key has a value associated in the cache
     */
    virtual void put(const K& key, const V& val) = 0;

    /**
     * Clear a cache entry associated with a key
     * @param key string, the key to lookup and clear
     *
     * @return boolean true if any elements are removed, else false
     */
    virtual bool remove(const K& key) = 0;

    /**
     * Empty the contents of the cache
     */
    virtual void clear() = 0;

    /**
     * return the size of the cache
     *
     * @returns size_t, the size of the cache
     */

    virtual size_t size() = 0;
};

}

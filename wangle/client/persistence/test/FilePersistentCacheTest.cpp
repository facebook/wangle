// Copyright 2004-present Facebook.  All rights reserved.
#include <wangle/client/persistence/FilePersistentCache.h>
#include <wangle/client/persistence/test/TestUtil.h>

using namespace std;
using namespace wangle;

TEST(FilePersistentCacheTest, stringTypesGetPutTest) {
  typedef string KeyType;
  typedef string ValType;
  vector<KeyType> keys = {"key1", "key2"};
  vector<ValType> values = {"value1", "value2"};
  testSimplePutGet<KeyType, ValType>(keys, values);
}

TEST(FilePersistentCacheTest, basicTypeGetPutTest) {
  typedef int KeyType;
  typedef double ValType;
  vector<KeyType> keys = {1, 2};
  vector<ValType> values = {3.0, 4.0};
  testSimplePutGet<KeyType, ValType>(keys, values);
}

TEST(FilePersistentCacheTest, stringCompositeGetPutTest) {
  typedef string KeyType;
  typedef list<string> ValType;
  vector<KeyType> keys = {"key1", "key2"};
  vector<ValType> values =
    {ValType({"fma", "shijin"}), ValType({"foo", "bar"})};
  testSimplePutGet<KeyType, ValType>(keys, values);
}

TEST(FilePersistentCacheTest, stringNestedValGetPutTest) {
  typedef string KeyType;
  typedef map<string, list<string>> ValType;
  vector<KeyType> keys = {"cool", "not cool"};
  vector<ValType> values = {
    ValType({
        {"NYC", {"fma", "shijin"}},
        {"MPK", {"ranjeeth", "dsp"}}
        }),
    ValType({
        {"MPK", {"subodh", "blake"}},
        {"STL", {"pgriess"}}
        }),
  };
  testSimplePutGet<KeyType, ValType>(keys, values);
}

TEST(FilePersistentCacheTest, stringNestedKeyValGetPutTest) {
  typedef pair<string, string> KeyType;
  typedef map<string, list<string>> ValType;
  vector<KeyType> keys = {
    make_pair("cool", "what the=?"),
    make_pair("not_cool", "how on *& earth?")
  };
  vector<ValType> values = {
    ValType({
        {"NYC", {"fma", "shijin kong$"}},
        {"MPK", {"ranjeeth", "dsp"}}
        }),
    ValType({
        {"MPK", {"subodh", "blake"}},
        {"STL", {"pgriess"}}
        }),
  };
  testSimplePutGet<KeyType, ValType>(keys, values);
}

template<typename K, typename V>
void testEmptyFile() {
  string filename = getPersistentCacheFilename();
  size_t cacheCapacity = 10;
  int fd = folly::openNoInt(
              filename.c_str(),
              O_WRONLY | O_CREAT | O_TRUNC,
              S_IRUSR | S_IWUSR
          );
  EXPECT_TRUE(fd != -1);
  typedef FilePersistentCache<K, V> CacheType;
  CacheType cache(filename, cacheCapacity, chrono::seconds(1));
  EXPECT_EQ(cache.size(), 0);
  EXPECT_TRUE(folly::closeNoInt(fd) != -1);
  EXPECT_TRUE(unlink(filename.c_str()) != -1);
}

TEST(FilePersistentCacheTest, stringTypesEmptyFile) {
  typedef string KeyType;
  typedef string ValType;
  testEmptyFile<KeyType, ValType>();
}

TEST(FilePersistentCacheTest, stringNestedValEmptyFile) {
  typedef string KeyType;
  typedef map<string, list<string>> ValType;
  testEmptyFile<KeyType, ValType>();
}

//TODO_ranjeeth : integrity, should we sign the file somehow t3623725
template<typename K, typename V>
void testInvalidFile(const std::string& content) {
  string filename = getPersistentCacheFilename();
  size_t cacheCapacity = 10;
  int fd = folly::openNoInt(
              filename.c_str(),
              O_WRONLY | O_CREAT | O_TRUNC,
              S_IRUSR | S_IWUSR
          );
  EXPECT_TRUE(fd != -1);
  EXPECT_EQ(
    folly::writeFull(fd, content.data(), content.size()),
    content.size()
  );
  typedef FilePersistentCache<K, V> CacheType;
  CacheType cache(filename, cacheCapacity, chrono::seconds(1));
  EXPECT_EQ(cache.size(), 0);
  EXPECT_TRUE(folly::closeNoInt(fd) != -1);
  EXPECT_TRUE(unlink(filename.c_str()) != -1);
}

TEST(FilePersistentCacheTest, stringTypesInvalidFile) {
  typedef string KeyType;
  typedef string ValType;
  testInvalidFile<KeyType, ValType>(string("{\"k1\":\"v1\",1}"));
}

TEST(FilePersistentCacheTest, stringNestedValInvalidFile) {
  typedef string KeyType;
  typedef map<string, list<string>> ValType;
  testInvalidFile<KeyType, ValType>(string("{\"k1\":\"v1\"}"));
}

//TODO_ranjeeth : integrity, should we sign the file somehow t3623725
template<typename K, typename V>
void testValidFile(
    const std::string& content,
    const vector<K>& keys,
    const vector<V>& values
  ) {
  string filename = getPersistentCacheFilename();
  size_t cacheCapacity = 10;
  int fd = folly::openNoInt(
              filename.c_str(),
              O_WRONLY | O_CREAT | O_TRUNC,
              S_IRUSR | S_IWUSR
          );
  EXPECT_TRUE(fd != -1);
  EXPECT_EQ(
    folly::writeFull(fd, content.data(), content.size()),
    content.size()
  );
  typedef FilePersistentCache<K, V> CacheType;
  CacheType cache(filename, cacheCapacity, chrono::seconds(1));
  EXPECT_EQ(cache.size(), keys.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    EXPECT_EQ(cache.get(keys[i]).value(), values[i]);
  }
  EXPECT_TRUE(folly::closeNoInt(fd) != -1);
  EXPECT_TRUE(unlink(filename.c_str()) != -1);
}

TEST(FilePersistentCacheTest, stringTypesValidFileTest) {
  typedef string KeyType;
  typedef string ValType;
  vector<KeyType> keys = {"key1", "key2"};
  vector<ValType> values = {"value1", "value2"};
  std::string content = "[[\"key1\",\"value1\"], [\"key2\",\"value2\"]]";
  testValidFile<KeyType, ValType>(content, keys, values);
}

TEST(FilePersistentCacheTest, basicEvictionTest) {
  string filename = getPersistentCacheFilename();
  {
    size_t cacheCapacity = 10;
    FilePersistentCache<int, int> cache(filename,
        cacheCapacity, chrono::seconds(1));
    for (int i = 0; i < 10; ++i) {
        cache.put(i, i);
    }
    EXPECT_EQ(cache.size(), 10); // MRU to LRU : 9, 8, ...1, 0

    cache.put(10, 10); // evicts 0
    EXPECT_EQ(cache.size(), 10);
    EXPECT_FALSE(cache.get(0).hasValue());
    EXPECT_EQ(cache.get(10).value(), 10); // MRU to LRU : 10, 9, ... 2, 1

    EXPECT_EQ(cache.get(1).value(), 1); // MRU to LRU : 1, 10, 9, ..., 3, 2
    cache.put(11, 11); // evicts 2
    EXPECT_EQ(cache.size(), 10);
    EXPECT_FALSE(cache.get(2).hasValue());
    EXPECT_EQ(cache.get(11).value(), 11);
  }

  EXPECT_TRUE(unlink(filename.c_str()) != -1);
}

// serialization has changed, so test if things will be alright afterwards
TEST(FilePersistentCacheTest, backwardCompatiblityTest) {
  typedef string KeyType;
  typedef string ValType;

  // write an old style file
  vector<KeyType> keys = {"key1", "key2"};
  vector<ValType> values = {"value1", "value2"};
  std::string content = "{\"key1\":\"value1\", \"key2\":\"value2\"}";
  typedef FilePersistentCache<KeyType, ValType> CacheType;
  string filename = getPersistentCacheFilename();
  size_t cacheCapacity = 10;
  int fd = folly::openNoInt(
              filename.c_str(),
              O_WRONLY | O_CREAT | O_TRUNC,
              S_IRUSR | S_IWUSR
          );
  EXPECT_TRUE(fd != -1);
  EXPECT_EQ(
    folly::writeFull(fd, content.data(), content.size()),
    content.size()
  );
  EXPECT_TRUE(folly::closeNoInt(fd) != -1);

  {
    // it should fail to load
    CacheType cache(filename, cacheCapacity, chrono::seconds(1));
    EXPECT_EQ(cache.size(), 0);

    // .. but new entries should work
    cache.put("key1", "value1");
    cache.put("key2", "value2");
    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.get("key1").value(), "value1");
    EXPECT_EQ(cache.get("key2").value(), "value2");
  }
  {
    // new format persists
    CacheType cache(filename, cacheCapacity, chrono::seconds(1));
    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.get("key1").value(), "value1");
    EXPECT_EQ(cache.get("key2").value(), "value2");
  }
  EXPECT_TRUE(unlink(filename.c_str()) != -1);
}

TEST(FilePersistentCacheTest, destroy) {
  typedef FilePersistentCache<int, int> CacheType;
  std::string cacheFile = getPersistentCacheFilename();

  auto cache1 = folly::make_unique<CacheType>(
    cacheFile, 10, std::chrono::seconds(3));
  cache1.reset();
  auto cache2 = folly::make_unique<CacheType>(
    cacheFile, 10, std::chrono::seconds(3));
  cache2.reset();
}

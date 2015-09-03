#include <wangle/client/persistence/test/TestUtil.h>

namespace wangle {

std::string getPersistentCacheFilename() {
  char filename[] = "/tmp/fbtls.XXXXXX";
  int fd = mkstemp(filename);
  close(fd);
  EXPECT_TRUE(unlink(filename) != -1);
  return std::string(filename);
}

}

#include <wangle/client/ssl/SSLSessionCacheUtils.h>

#include <folly/io/IOBuf.h>
#include <memory>

namespace wangle {

SSL_SESSION* fbStringToSession(const folly::fbstring& str) {
  auto sessionData = reinterpret_cast<const unsigned char*>(str.c_str());
  // Create a SSL_SESSION and return. In failure it returns nullptr.
  return d2i_SSL_SESSION(nullptr, &sessionData, str.length());
}

// serialize and deserialize session data
folly::Optional<folly::fbstring> sessionToFbString(SSL_SESSION* session) {
  if (!session) {
    return folly::Optional<folly::fbstring>();
  }
  // Get the length first, then we know how much space to allocate. An invalid
  // session returns a zero len.
  int len = i2d_SSL_SESSION(session, nullptr);
  if (len > 0) {
    try {
      auto sessionData = folly::IOBuf::create(len);
      // i2d_SSL_SESSION increments the pointer pointed to by buf to point one
      // byte after the saved data (unfortunately, the man page isn't very
      // clear on this point). That's why we need to save the sessionData in
      // advance because we will not be able to get it after this call.
      //
      // Check this reference:
      //   http://stackoverflow.com/questions/4281992/
      //   fail-to-use-d2i-ssl-session-to-unserialise-ssl-session
      auto buf = reinterpret_cast<unsigned char*>(sessionData->writableData());
      len = i2d_SSL_SESSION(session, &buf);
      if (len > 0) {
        sessionData->append(len);
        return folly::Optional<folly::fbstring>(sessionData->moveToFbString());
      }
    } catch (const std::bad_alloc& ex) {
      LOG(ERROR) << "Failed to allocate memory for sessionData: " << ex.what();
    }
  }

  return folly::Optional<folly::fbstring>();
}

}

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
#include <wangle/client/ssl/SSLSessionCacheUtils.h>

#include <memory>

#include <folly/io/IOBuf.h>
#include <wangle/ssl/SSLUtil.h>

namespace wangle {

namespace {
static int32_t getSessionServiceIdentityIdx() {
  static int32_t index = []{
    int result = -1;
    SSLUtil::getSSLSessionExStrIndex(&result);
    return result;
  }();
  return index;
}

static SSL_SESSION* fbStringToSession(const folly::fbstring& str) {
  auto sessionData = reinterpret_cast<const unsigned char*>(str.c_str());
  // Create a SSL_SESSION and return. In failure it returns nullptr.
  return d2i_SSL_SESSION(nullptr, &sessionData, str.length());
}

// serialize and deserialize session data
static
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
        // Update the amount of data in the IOBuf
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

bool
setSessionServiceIdentity(SSL_SESSION* session, const std::string& str) {
  if (!session || str.empty()) {
    return false;
  }
  auto serviceExData = new std::string(str);
  return SSL_SESSION_set_ex_data(
    session, getSessionServiceIdentityIdx(), serviceExData) > 0;
}

folly::Optional<std::string>
getSessionServiceIdentity(SSL_SESSION* session) {
  if (!session) {
    return folly::Optional<std::string>();
  }
  auto data = SSL_SESSION_get_ex_data(session, getSessionServiceIdentityIdx());
  if (!data) {
    return folly::Optional<std::string>();
  }
  return *(reinterpret_cast<std::string*>(data));
}

folly::Optional<SSLSessionCacheData> getCacheDataForSession(SSL_SESSION* sess) {
  auto sessionData = sessionToFbString(sess);
  if (!sessionData) {
    return folly::Optional<SSLSessionCacheData>();
  }
  SSLSessionCacheData result;
  result.sessionData = std::move(*sessionData);
  auto serviceIdentity = getSessionServiceIdentity(sess);
  if (serviceIdentity) {
    result.serviceIdentity = std::move(*serviceIdentity);
  }
#ifdef WANGLE_HAVE_SSL_SESSION_DUP
  result.sessionDuplicateTemplate =
      std::shared_ptr<SSL_SESSION>(SSL_SESSION_dup(sess), SessionDestructor{});
#endif
  return result;
}

SSL_SESSION* getSessionFromCacheData(const SSLSessionCacheData& data) {
#ifdef WANGLE_HAVE_SSL_SESSION_DUP
  if (data.sessionDuplicateTemplate) {
    return SSL_SESSION_dup(data.sessionDuplicateTemplate.get());
  }
#endif
  auto result = fbStringToSession(data.sessionData);
  if (!result) {
    return nullptr;
  }
  setSessionServiceIdentity(result, data.serviceIdentity.toStdString());
  return result;
}

SSL_SESSION* cloneSSLSession(SSL_SESSION* toClone) {
  if (!toClone) {
    return nullptr;
  }

#ifdef WANGLE_HAVE_SSL_SESSION_DUP
  return SSL_SESSION_dup(toClone);
#else
  auto sessionData = sessionToFbString(toClone);
  if (!sessionData) {
    return nullptr;
  }
  auto clone = fbStringToSession(std::move(*sessionData));

  if (!clone) {
    return nullptr;
  }
  auto serviceIdentity = getSessionServiceIdentity(toClone);
  if (serviceIdentity) {
    setSessionServiceIdentity(clone, serviceIdentity.value());
  }
  return clone;
#endif
}

}

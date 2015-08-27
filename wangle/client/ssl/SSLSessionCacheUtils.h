#pragma once

#include <folly/FBString.h>
#include <folly/Optional.h>
#include <openssl/ssl.h>

namespace wangle {

// serialize and deserialize session data
folly::Optional<folly::fbstring> sessionToFbString(SSL_SESSION* session);
SSL_SESSION* fbStringToSession(const folly::fbstring& str);

}

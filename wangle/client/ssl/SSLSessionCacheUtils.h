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

#include <folly/FBString.h>
#include <folly/Optional.h>
#include <openssl/ssl.h>
#include <wangle/client/ssl/SSLSessionCacheData.h>

namespace wangle {

// Service identity access on the session.
folly::Optional<std::string> getSessionServiceIdentity(SSL_SESSION* sess);
bool setSessionServiceIdentity(SSL_SESSION* sess, const std::string& str);

// Helpers to convert SSLSessionCacheData to/from SSL_SESSION
folly::Optional<SSLSessionCacheData> getCacheDataForSession(SSL_SESSION* sess);
SSL_SESSION* getSessionFromCacheData(const SSLSessionCacheData& data);

// Does a clone of just the session data and service identity
// Internal links to SSL structs are not kept
SSL_SESSION* cloneSSLSession(SSL_SESSION* toClone);

}

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

#include <memory>

// This must come before any includes of OpenSSL.
#include <folly/portability/Windows.h>

#include <openssl/ssl.h>

namespace wangle {

class SessionDestructor {
 public:
   void operator()(SSL_SESSION* session) {
     if (session) {
       SSL_SESSION_free(session);
     }
   }
};

typedef std::unique_ptr<SSL_SESSION, SessionDestructor> SSLSessionPtr;

};

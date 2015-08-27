#pragma once

#include <memory>
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

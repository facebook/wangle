#pragma once

#include <string>
#include <glog/logging.h>
#include <wangle/client/ssl/SSLSession.h>

namespace wangle {

std::vector<std::pair<SSL_SESSION*, size_t>> getSessions();

std::pair<SSL_SESSION*, size_t> getSessionWithTicket();

SSLSessionPtr createPersistentTestSession(
    std::pair<SSL_SESSION*, size_t> session);

std::string getSessionData(SSL_SESSION* s, size_t expectedLength);

bool isSameSession(
    std::pair<SSL_SESSION*, size_t> lhs,
    std::pair<SSL_SESSION*, size_t> rhs);

}

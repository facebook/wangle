// Copyright 2004-present Facebook.  All rights reserved.
//
#include <wangle/client/ssl/SSLSessionCacheData.h>

using namespace std::chrono;

namespace folly {

template<>
folly::dynamic toDynamic(const wangle::SSLSessionCacheData& data) {
  folly::dynamic ret = folly::dynamic::object;
  ret["session_data"] = folly::dynamic(data.sessionData);
  system_clock::duration::rep rep = data.addedTime.time_since_epoch().count();
  ret["added_time"] = folly::dynamic(static_cast<uint64_t>(rep));
  return ret;
}

template<>
wangle::SSLSessionCacheData convertTo(const dynamic& d) {
  wangle::SSLSessionCacheData data;
  data.sessionData = d["session_data"].asString();
  data.addedTime =
    system_clock::time_point(system_clock::duration(d["added_time"].asInt()));
  return data;
}

} // folly

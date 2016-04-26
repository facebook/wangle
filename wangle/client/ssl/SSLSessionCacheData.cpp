/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
//
#include <wangle/client/ssl/SSLSessionCacheData.h>

using namespace std::chrono;

namespace folly {

template<>
folly::dynamic toDynamic(const wangle::SSLSessionCacheData& data) {
  folly::dynamic ret = folly::dynamic::object;
  ret["session_data"] = folly::dynamic(data.sessionData.toStdString());
  system_clock::duration::rep rep = data.addedTime.time_since_epoch().count();
  ret["added_time"] = folly::dynamic(static_cast<uint64_t>(rep));
  ret["service_identity"] = folly::dynamic(data.serviceIdentity.toStdString());
  return ret;
}

template<>
wangle::SSLSessionCacheData convertTo(const dynamic& d) {
  wangle::SSLSessionCacheData data;
  data.sessionData = d["session_data"].asString();
  data.addedTime =
    system_clock::time_point(system_clock::duration(d["added_time"].asInt()));
  data.serviceIdentity = d.getDefault("service_identity", "").asString();
  return data;
}

} // folly

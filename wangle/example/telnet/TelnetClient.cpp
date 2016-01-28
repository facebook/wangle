/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <iostream>
#include <gflags/gflags.h>

#include <wangle/bootstrap/ClientBootstrap.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/codec/LineBasedFrameDecoder.h>
#include <wangle/codec/StringCodec.h>

using namespace folly;
using namespace wangle;

DEFINE_int32(port, 23, "test telnet server port");
DEFINE_string(host, "::1", "test telnet server address");

typedef Pipeline<folly::IOBufQueue&, std::string> TelnetPipeline;

class TelnetHandler : public HandlerAdapter<std::string> {
 public:
  virtual void read(Context* ctx, std::string msg) override {
    std::cout << msg;
  }
  virtual void readException(Context* ctx, exception_wrapper e) override {
    std::cout << exceptionStr(e) << std::endl;
    close(ctx);
  }
  virtual void readEOF(Context* ctx) override {
    std::cout << "EOF received :(" << std::endl;
    close(ctx);
  }
};

class TelnetPipelineFactory : public PipelineFactory<TelnetPipeline> {
 public:
  TelnetPipeline::Ptr newPipeline(std::shared_ptr<AsyncTransportWrapper> sock) {
    auto pipeline = TelnetPipeline::create();
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(EventBaseHandler()); // ensure we can write from any thread
    pipeline->addBack(LineBasedFrameDecoder(8192, false));
    pipeline->addBack(StringCodec());
    pipeline->addBack(TelnetHandler());
    pipeline->finalize();

    return pipeline;
  }
};

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  ClientBootstrap<TelnetPipeline> client;
  client.group(std::make_shared<wangle::IOThreadPoolExecutor>(1));
  client.pipelineFactory(std::make_shared<TelnetPipelineFactory>());
  auto pipeline = client.connect(SocketAddress(FLAGS_host,FLAGS_port)).get();

  try {
    while (true) {
      std::string line;
      std::getline(std::cin, line);
      if (line == "") {
        break;
      }

      // Sync write will throw exception if server goes away
      pipeline->write(line + "\r\n").get();
      if (line == "bye") {
        pipeline->close();
        break;
      }
    }
  } catch(const std::exception& e) {
    std::cout << exceptionStr(e) << std::endl;
  }

  return 0;
}

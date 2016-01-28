/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gflags/gflags.h>

#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/codec/LineBasedFrameDecoder.h>
#include <wangle/codec/StringCodec.h>

using namespace folly;
using namespace wangle;

DEFINE_int32(port, 23, "test telnet server port");

typedef Pipeline<IOBufQueue&, std::string> TelnetPipeline;

class TelnetHandler : public HandlerAdapter<std::string> {
 public:
  virtual void read(Context* ctx, std::string msg) override {
    if (msg.empty()) {
      write(ctx, "Please type something.\r\n");
    } else if (msg == "bye") {
      write(ctx, "Have a fabulous day!\r\n").then([ctx, this]{
        close(ctx);
      });
    } else {
      write(ctx, "Did you say '" + msg + "'?\r\n");
    }
  }

  virtual void transportActive(Context* ctx) override {
    auto sock = ctx->getTransport();
    SocketAddress localAddress;
    sock->getLocalAddress(&localAddress);
    write(ctx, "Welcome to " + localAddress.describe() + "!\r\n");
    write(ctx, "Type 'bye' to disconnect.\r\n");
  }
};

class TelnetPipelineFactory : public PipelineFactory<TelnetPipeline> {
 public:
  TelnetPipeline::Ptr newPipeline(std::shared_ptr<AsyncTransportWrapper> sock) {
    auto pipeline = TelnetPipeline::create();
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(LineBasedFrameDecoder(8192));
    pipeline->addBack(StringCodec());
    pipeline->addBack(TelnetHandler());
    pipeline->finalize();

    return pipeline;
  }
};

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  ServerBootstrap<TelnetPipeline> server;
  server.childPipeline(std::make_shared<TelnetPipelineFactory>());
  server.bind(FLAGS_port);
  server.waitForStop();

  return 0;
}

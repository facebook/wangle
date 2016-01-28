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
#include <wangle/channel/FileRegion.h>
#include <wangle/codec/LineBasedFrameDecoder.h>
#include <wangle/codec/StringCodec.h>
#include <sys/sendfile.h>

using namespace folly;
using namespace wangle;

DEFINE_int32(port, 11219, "test file server port");

typedef Pipeline<IOBufQueue&, std::string> FileServerPipeline;

class FileServerHandler : public HandlerAdapter<std::string> {
 public:
  void read(Context* ctx, std::string filename) override {
    if (filename == "bye") {
      close(ctx);
    }

    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) {
      write(ctx, sformat("Error opening {}: {}\r\n",
                         filename,
                         strerror(errno)));
      return;
    }

    struct stat buf;
    if (fstat(fd, &buf) == -1) {
      write(ctx, sformat("Could not stat file {}: {}\r\n",
                         filename,
                         strerror(errno)));
      return;
    }

    FileRegion fileRegion(fd, 0, buf.st_size);
    auto guard = ctx->getPipelineShared();
    fileRegion.transferTo(ctx->getTransport())
      .onError([this, guard, ctx, filename](const std::exception& e){
        write(ctx, sformat("Error sending file {}: {}\r\n",
                           filename,
                           exceptionStr(e)));
      });
  }

  void readException(Context* ctx, exception_wrapper ew) override {
    write(ctx, sformat("Error: {}\r\n", exceptionStr(ew))).then([this, ctx]{
      close(ctx);
    });
  }

  void transportActive(Context* ctx) override {
    SocketAddress localAddress;
    ctx->getTransport()->getLocalAddress(&localAddress);
    write(ctx, "Welcome to " + localAddress.describe() + "!\r\n");
    write(ctx, "Type the name of a file and it will be streamed to you!\r\n");
    write(ctx, "Type 'bye' to exit.\r\n");
  }
};

class FileServerPipelineFactory : public PipelineFactory<FileServerPipeline> {
 public:
  FileServerPipeline::Ptr newPipeline(
      std::shared_ptr<AsyncTransportWrapper> sock) {
    auto pipeline = FileServerPipeline::create();
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(LineBasedFrameDecoder());
    pipeline->addBack(StringCodec());
    pipeline->addBack(FileServerHandler());
    pipeline->finalize();

    return pipeline;
  }
};

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  ServerBootstrap<FileServerPipeline> server;
  server.childPipeline(std::make_shared<FileServerPipelineFactory>());
  server.bind(FLAGS_port);
  server.waitForStop();

  return 0;
}

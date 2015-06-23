/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gflags/gflags.h>

#include <wangle/bootstrap/AcceptRoutingHandler.h>
#include <wangle/bootstrap/RoutingDataHandler.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/AsyncSocketHandler.h>

using namespace folly;
using namespace folly::wangle;

DEFINE_int32(port, 23, "test server port");

/**
 * A simple server that hashes connections to worker threads
 * based on the first character typed in by the client.
 */

class NaiveRoutingDataHandler : public RoutingDataHandler {
 public:
  NaiveRoutingDataHandler(uint64_t connId, Callback* cob)
      : RoutingDataHandler(connId, cob) {}

  bool parseRoutingData(folly::IOBufQueue& bufQueue,
                        RoutingData& routingData) override {
    if (bufQueue.chainLength() == 0) {
      return false;
    }

    auto buf = bufQueue.move();
    buf->coalesce();
    // Use the first byte for hashing to a worker
    routingData.routingData = buf->data()[0];
    routingData.bufQueue.append(std::move(buf));
    return true;
  }
};

class NaiveRoutingDataHandlerFactory : public RoutingDataHandlerFactory {
 public:
  std::shared_ptr<RoutingDataHandler> newHandler(
      uint64_t connId, RoutingDataHandler::Callback* cob) override {
    return std::make_shared<NaiveRoutingDataHandler>(connId, cob);
  }
};

class ThreadPrintingHandler : public BytesToBytesHandler {
 public:
  explicit ThreadPrintingHandler(const std::string& routingData)
      : routingData_(routingData) {}

  virtual void transportActive(Context* ctx) override {
    std::stringstream out;
    out << "You were hashed to thread " << std::this_thread::get_id()
        << " based on '" << routingData_ << "'" << std::endl;
    write(ctx, IOBuf::copyBuffer(out.str()));
    close(ctx);
  }

 private:
  std::string routingData_;
};

class ServerPipelineFactory
    : public RoutingDataPipelineFactory<DefaultPipeline> {
 public:
  DefaultPipeline::UniquePtr newPipeline(std::shared_ptr<AsyncSocket> sock,
                                         const std::string& routingData) {
    DefaultPipeline::UniquePtr pipeline(new DefaultPipeline);
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(ThreadPrintingHandler(routingData));
    pipeline->finalize();

    return std::move(pipeline);
  }
};

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto routingHandlerFactory =
      std::make_shared<NaiveRoutingDataHandlerFactory>();
  auto childPipelineFactory = std::make_shared<ServerPipelineFactory>();

  ServerBootstrap<DefaultPipeline> server;
  server.pipeline(
      std::make_shared<AcceptRoutingPipelineFactory<DefaultPipeline>>(
          &server, routingHandlerFactory, childPipelineFactory));
  server.bind(FLAGS_port);
  server.waitForStop();

  return 0;
}

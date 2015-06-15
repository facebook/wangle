/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
  std::unique_ptr<RoutingDataHandler> newHandler(
      uint64_t connId, RoutingDataHandler::Callback* cob) override {
    return folly::make_unique<NaiveRoutingDataHandler>(connId, cob);
  }
};

class ThreadPrintingHandler : public BytesToBytesHandler {
 public:
  virtual void transportActive(Context* ctx) override {
    auto out = std::string("You were hashed to thread ") +
      folly::to<std::string>(pthread_self()) + "\n";
    write(ctx, IOBuf::copyBuffer(out));
    close(ctx);
  }
};

class ServerPipelineFactory : public PipelineFactory<DefaultPipeline> {
 public:
  DefaultPipeline::UniquePtr newPipeline(std::shared_ptr<AsyncSocket> sock) {
    DefaultPipeline::UniquePtr pipeline(new DefaultPipeline);
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(ThreadPrintingHandler());
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

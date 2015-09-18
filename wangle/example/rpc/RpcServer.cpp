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

#include <wangle/service/Service.h>
#include <wangle/service/ExpiringFilter.h>
#include <wangle/service/ExecutorFilter.h>
#include <wangle/service/ServerDispatcher.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/codec/LengthFieldBasedFrameDecoder.h>
#include <wangle/codec/LengthFieldPrepender.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/concurrent/CPUThreadPoolExecutor.h>

#include <wangle/example/rpc/SerializeHandler.h>

using namespace folly;
using namespace wangle;
using thrift::test::Bonk;

DEFINE_int32(port, 8080, "test server port");

typedef wangle::Pipeline<IOBufQueue&, Bonk> SerializePipeline;

class RpcService : public Service<Bonk> {
 public:
  virtual Future<Bonk> operator()(Bonk request) {
    // Oh no, we got Bonked!  Quick, Bonk back
    printf("Bonk: %s, %i\n", request.message.c_str(), request.type);

    /* sleep override: ignore lint
     * useful for testing dispatcher behavior by hand
     */
    // Wait for a bit
    return futures::sleep(std::chrono::seconds(request.type))
      .then([request]() {
        Bonk response;
        response.message = "Stop saying " + request.message + "!";
        response.type = request.type;
        return response;
      });
  }
};

class RpcPipelineFactory : public PipelineFactory<SerializePipeline> {
 public:
  SerializePipeline::Ptr newPipeline(std::shared_ptr<AsyncSocket> sock) {
    auto pipeline = SerializePipeline::create();
    pipeline->addBack(AsyncSocketHandler(sock));
    // ensure we can write from any thread
    pipeline->addBack(EventBaseHandler());
    pipeline->addBack(LengthFieldBasedFrameDecoder());
    pipeline->addBack(LengthFieldPrepender());
    pipeline->addBack(SerializeHandler());
    // We could use a serial dispatcher instead easily
    // pipeline->addBack(SerialServerDispatcher<Bonk>(&service_));
    // Or a Pipelined Dispatcher
    //pipeline->addBack(PipelinedServerDispatcher<Bonk>(&service_));
    pipeline->addBack(MultiplexServerDispatcher<Bonk>(&service_));
    pipeline->finalize();

    return std::move(pipeline);
  }

 private:
  ExecutorFilter<Bonk> service_{
    std::make_shared<CPUThreadPoolExecutor>(10),
      std::make_shared<RpcService>()};
};

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  ServerBootstrap<SerializePipeline> server;
  server.childPipeline(std::make_shared<RpcPipelineFactory>());
  server.bind(FLAGS_port);
  server.waitForStop();

  return 0;
}

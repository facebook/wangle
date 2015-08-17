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
  std::unique_ptr<SerializePipeline, folly::DelayedDestruction::Destructor>
  newPipeline(std::shared_ptr<AsyncSocket> sock) {

    std::unique_ptr<SerializePipeline, folly::DelayedDestruction::Destructor>
      pipeline(new SerializePipeline);
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

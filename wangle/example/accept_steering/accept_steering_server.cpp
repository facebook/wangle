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

#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/codec/LineBasedFrameDecoder.h>
#include <wangle/codec/StringCodec.h>

using namespace folly;
using namespace folly::wangle;

DEFINE_int32(port, 23, "test server port");

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
  std::unique_ptr<DefaultPipeline, folly::DelayedDestruction::Destructor>
  newPipeline(std::shared_ptr<AsyncSocket> sock) {

    std::unique_ptr<DefaultPipeline, folly::DelayedDestruction::Destructor>
      pipeline(new DefaultPipeline);
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(ThreadPrintingHandler());
    pipeline->finalize();

    return std::move(pipeline);
  }
};

typedef wangle::Pipeline<void*> AcceptPipeline;

class AcceptHandler : public wangle::InboundHandler<void*> {
  ServerPipelineFactory factory_;
  ServerBootstrap<DefaultPipeline>* server_;

 public:
  explicit AcceptHandler(ServerBootstrap<DefaultPipeline>* server)
      : server_(server) {
  }

  void read(Context* ctx, void* conn) {
    // Get list of acceptors. TODO: make this easier
    std::vector<Acceptor*> acceptors_;
    server_->forEachWorker([&](Acceptor* acceptor) {
      acceptors_.push_back(acceptor);
    });

    AsyncSocket* transport = (AsyncSocket*)conn;
    auto out = std::string("Accepted conn, hashing based on address...\n");
    transport->writeChain(nullptr, IOBuf::copyBuffer(out));

    // awesome hash function
    SocketAddress address;
    transport->getPeerAddress(&address);
    char a = address.getAddressStr()[0];
    Acceptor* acceptor = acceptors_[a % acceptors_.size()];

    // Since we are on a new thread, we have to update async socket's
    // event base
    transport->detachEventBase();

    // Switch to new acceptor's thread
    acceptor->getEventBase()->runInEventBaseThread([=](){
      transport->attachEventBase(acceptor->getEventBase());

      // TODO: pass in pipeline, instead of having AcceptPipeline create it?
      std::unique_ptr<DefaultPipeline,
                      folly::DelayedDestruction::Destructor>
        pipeline(factory_.newPipeline(
                   std::shared_ptr<AsyncSocket>(
                     transport,
                     folly::DelayedDestruction::Destructor())));
      pipeline->transportActive();
      auto connection = new ServerAcceptor<DefaultPipeline>::ServerConnection(
        std::move(pipeline));

      acceptor->addConnection(connection);
    });
  }
};

class AcceptSteeringPipelineFactory : public PipelineFactory<AcceptPipeline> {
 public:
  explicit AcceptSteeringPipelineFactory(
    ServerBootstrap<DefaultPipeline>* server)
      : server_(server) {}

  std::unique_ptr<AcceptPipeline, folly::DelayedDestruction::Destructor>
  newPipeline(std::shared_ptr<AsyncSocket>) {

    std::unique_ptr<AcceptPipeline, folly::DelayedDestruction::Destructor>
      pipeline(new AcceptPipeline);
    pipeline->addBack(AcceptHandler(server_));

    return std::move(pipeline);
  }

 private:
  ServerBootstrap<DefaultPipeline>* server_;
};

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  ServerBootstrap<DefaultPipeline> server;
  server.pipeline(std::make_shared<AcceptSteeringPipelineFactory>(&server));
  server.bind(FLAGS_port);
  server.waitForStop();

  return 0;
}

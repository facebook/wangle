/*
 * Copyright 2017-present Facebook, Inc.
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

#include <wangle/bootstrap/ClientBootstrap.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <wangle/channel/AsyncSocketHandler.h>

using namespace folly;
using namespace wangle;

DEFINE_int32(port, 1080, "proxy server port");
DEFINE_string(remote_host, "127.0.0.1", "remote host");
DEFINE_int32(remote_port, 23, "remote port");

class ProxyBackendHandler : public InboundBytesToBytesHandler {
 public:
  explicit ProxyBackendHandler(DefaultPipeline* frontendPipeline) :
      frontendPipeline_(frontendPipeline) {}

  void read(Context*, IOBufQueue& q) override {
    frontendPipeline_->write(q.move());
  }

  void readEOF(Context*) override {
    LOG(INFO) << "Connection closed by remote host";
    frontendPipeline_->close();
  }

  void readException(Context*, exception_wrapper e) override {
    LOG(ERROR) << "Remote error: " << exceptionStr(e);
    frontendPipeline_->close();
  }

 private:
  DefaultPipeline* frontendPipeline_;
};

class ProxyBackendPipelineFactory : public PipelineFactory<DefaultPipeline> {
 public:
  explicit ProxyBackendPipelineFactory(DefaultPipeline* frontendPipeline) :
      frontendPipeline_(frontendPipeline) {}

  DefaultPipeline::Ptr newPipeline(
      std::shared_ptr<AsyncTransportWrapper> sock) override {
    auto pipeline = DefaultPipeline::create();
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(ProxyBackendHandler(frontendPipeline_));
    pipeline->finalize();

    return pipeline;
  }
 private:
  DefaultPipeline* frontendPipeline_;
};

class ProxyFrontendHandler : public BytesToBytesHandler {
 public:
  explicit ProxyFrontendHandler(SocketAddress remoteAddress) :
      remoteAddress_(remoteAddress) {}

  void read(Context*, IOBufQueue& q) override {
    backendPipeline_->write(q.move());
  }

  void readEOF(Context* ctx) override {
    LOG(INFO) << "Connection closed by local host";
    backendPipeline_->close().then([this, ctx](){
      this->close(ctx);
    });
  }

  void readException(Context* ctx, exception_wrapper e) override {
    LOG(ERROR) << "Local error: " << exceptionStr(e);
    backendPipeline_->close().then([this, ctx](){
      this->close(ctx);
    });
  }

  void transportActive(Context* ctx) override {
    if (backendPipeline_) {
      // Already connected
      return;
    }

    // Pause reading from the socket until remote connection succeeds
    auto frontendPipeline = dynamic_cast<DefaultPipeline*>(ctx->getPipeline());
    frontendPipeline->transportInactive();

    client_.pipelineFactory(
        std::make_shared<ProxyBackendPipelineFactory>(frontendPipeline));
    client_.connect(remoteAddress_)
      .then([this, frontendPipeline](DefaultPipeline* pipeline){
        backendPipeline_ = pipeline;
        // Resume read
        frontendPipeline->transportActive();
      })
      .onError([this, ctx](const std::exception& e){
        LOG(ERROR) << "Connect error: " << exceptionStr(e);
        this->close(ctx);
      });
  }

 private:
  SocketAddress remoteAddress_;
  ClientBootstrap<DefaultPipeline> client_;
  DefaultPipeline* backendPipeline_;
};

class ProxyFrontendPipelineFactory : public PipelineFactory<DefaultPipeline> {
 public:
  explicit ProxyFrontendPipelineFactory(SocketAddress remoteAddress) :
      remoteAddress_(remoteAddress) {}

  DefaultPipeline::Ptr newPipeline(
      std::shared_ptr<AsyncTransportWrapper> sock) override {
    auto pipeline = DefaultPipeline::create();
    pipeline->addBack(AsyncSocketHandler(sock));
    pipeline->addBack(std::make_shared<ProxyFrontendHandler>(remoteAddress_));
    pipeline->finalize();

    return pipeline;
  }
 private:
  SocketAddress remoteAddress_;
};

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  ServerBootstrap<DefaultPipeline> server;
  server.childPipeline(std::make_shared<ProxyFrontendPipelineFactory>(
      SocketAddress(FLAGS_remote_host, FLAGS_remote_port)));
  server.bind(FLAGS_port);
  server.waitForStop();

  return 0;
}

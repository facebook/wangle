/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <wangle/channel/Handler.h>
#include <wangle/service/Service.h>

namespace wangle {

/**
 * Dispatch requests from pipeline one at a time synchronously.
 * Concurrent requests are queued in the pipeline.
 */
template <typename Req, typename Resp = Req>
class SerialServerDispatcher : public HandlerAdapter<Req, Resp> {
 public:

  typedef typename HandlerAdapter<Req, Resp>::Context Context;

  explicit SerialServerDispatcher(Service<Req, Resp>* service)
      : service_(service) {}

  void read(Context* ctx, Req in) override {
    auto resp = (*service_)(std::move(in)).get();
    ctx->fireWrite(std::move(resp));
  }

 private:

  Service<Req, Resp>* service_;
};

/**
 * Dispatch requests from pipeline as they come in.
 * Responses are queued until they can be sent in order.
 */
template <typename Req, typename Resp = Req>
class PipelinedServerDispatcher : public HandlerAdapter<Req, Resp> {
 public:

  typedef typename HandlerAdapter<Req, Resp>::Context Context;

  explicit PipelinedServerDispatcher(Service<Req, Resp>* service)
      : service_(service) {}

  void read(Context*, Req in) override {
    auto requestId = requestId_++;
    (*service_)(std::move(in)).then([requestId,this](Resp& resp){
      responses_[requestId] = resp;
      sendResponses();
    });
  }

  void sendResponses() {
    auto search = responses_.find(lastWrittenId_+1);
    while (search != responses_.end()) {
      Resp resp = std::move(search->second);
      responses_.erase(search->first);
      this->getContext()->fireWrite(std::move(resp));
      lastWrittenId_++;
      search = responses_.find(lastWrittenId_+1);
    }
  }

 private:
  Service<Req, Resp>* service_;
  uint32_t requestId_{1};
  std::unordered_map<uint32_t, Resp> responses_;
  uint32_t lastWrittenId_{0};
};
 
 
template <typename Req, typename Resp = Req>
class MultiplexServerDispatcherGuard{
public:
	typedef typename HandlerAdapter<Req, Resp>::Context Context;
	MultiplexServerDispatcherGuard(Context* ctx,std::shared_ptr<PipelineBase> pipeline)
 :ctx_(ctx), pipeline_(pipeline){}
	std::shared_ptr<PipelineBase> pipeline(){
		return pipeline_;
	}
	Context* context(){
		return ctx_;
	}
private:
	std::shared_ptr<PipelineBase> pipeline_;
	Context *ctx_;
};
/**
 * Dispatch requests from pipeline as they come in.  Concurrent
 * requests are assumed to have sequence id's that are taken care of
 * by the pipeline.  Unlike a multiplexed client dispatcher, a
 * multiplexed server dispatcher needs no state, and the sequence id's
 * can just be copied from the request to the response in the pipeline.
 */
template <typename Req, typename Resp = Req>
class MultiplexServerDispatcher : public HandlerAdapter<Req, Resp> {
 public:

  typedef typename HandlerAdapter<Req, Resp>::Context Context;

  explicit MultiplexServerDispatcher(Service<Req, Resp>* service)
      : service_(service) {}

  void read(Context* ctx, Req in) override {
    auto guard = std::make_shared<MultiplexServerDispatcherGuard<Req, Resp>>(ctx, ctx->getPipelineShared());
    (*service_)(std::move(in)).then([ctx](Resp resp) {
       auto pipe = bone->pipeline();
	      bone->context()->fireWrite(std::move(resp));
	      bone->context()->getTransport()->getEventBase()->runInEventBaseThread([pipe = std::move(pipe)]{/*nothing but free pipeline in io thread*/});
    });
  }
  //there must be some way to free pipeline, otherwise the pipeline never freed
	 virtual void readEOF(Context* ctx) {	
		    ctx->getPipeline()->getPipelineManager()->deletePipeline(ctx->getPipeline());
  	}
  	virtual void readException(Context* ctx, folly::exception_wrapper e) {
    	ctx->getPipeline()->getPipelineManager()->deletePipeline(ctx->getPipeline());
  	}
 private:
  Service<Req, Resp>* service_;
};

} // namespace wangle

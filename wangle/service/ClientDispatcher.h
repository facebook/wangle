/*
 *  Copyright (c) 2015, Facebook, Inc.
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

namespace folly { namespace wangle {

/**
 * Dispatch a request, satisfying Promise `p` with the response;
 * the returned Future is satisfied when the response is received:
 * only one request is allowed at a time.
 */
template <typename Pipeline, typename Req, typename Resp = Req>
class SerialClientDispatcher : public HandlerAdapter<Req, Resp>
                             , public Service<Req, Resp> {
 public:

  typedef typename HandlerAdapter<Req, Resp>::Context Context;

  void setPipeline(Pipeline* pipeline) {
    pipeline_ = pipeline;
    pipeline->addBack(this);
    pipeline->finalize();
  }

  void read(Context* ctx, Req in) override {
    DCHECK(p_);
    p_->setValue(std::move(in));
    p_ = none;
  }

  virtual Future<Resp> operator()(Req arg) override {
    CHECK(!p_);
    DCHECK(pipeline_);

    p_ = Promise<Resp>();
    auto f = p_->getFuture();
    pipeline_->write(std::move(arg));
    return f;
  }

  virtual Future<Unit> close() override {
    return HandlerAdapter<Req, Resp>::close(nullptr);
  }

  virtual Future<Unit> close(Context* ctx) override {
    return HandlerAdapter<Req, Resp>::close(ctx);
  }
 private:
  Pipeline* pipeline_{nullptr};
  folly::Optional<Promise<Resp>> p_;
};

}} // namespace

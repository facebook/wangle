/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <type_traits>

#include <wangle/channel/Pipeline.h>

namespace wangle {

/*
 * StaticPipeline allows you to create a Pipeline with minimal allocations.
 * Specify your handlers after the input/output types of your Pipeline in order
 * from front to back, and construct with either H&&, H*, or std::shared_ptr<H>
 * for each handler. The pipeline will be finalized for you at the end of
 * construction. For example:
 *
 * StringToStringHandler stringHandler1;
 * auto stringHandler2 = std::make_shared<StringToStringHandler>();
 *
 * StaticPipeline<int, std::string,
 *   IntToStringHandler,
 *   StringToStringHandler,
 *   StringToStringHandler>(
 *     IntToStringHandler(),  // H&&
 *     &stringHandler1,       // H*
 *     stringHandler2)        // std::shared_ptr<H>
 * pipeline;
 *
 * You can then use pipeline just like any Pipeline. See Pipeline.h.
 */
template <class R, class W, class... Handlers>
class StaticPipeline;

template <class R, class W>
class StaticPipeline<R, W> : public Pipeline<R, W> {
 protected:
  explicit StaticPipeline(bool) : Pipeline<R, W>(true) {}
  void initialize() {
    Pipeline<R, W>::finalize();
  }
};

template <class Handler>
class BaseWithOptional {
 protected:
  folly::Optional<Handler> handler_;
};

template <class Handler>
class BaseWithoutOptional {
};

template <class R, class W, class Handler, class... Handlers>
class StaticPipeline<R, W, Handler, Handlers...>
    : public StaticPipeline<R, W, Handlers...>
    , public std::conditional<std::is_abstract<Handler>::value,
                              BaseWithoutOptional<Handler>,
                              BaseWithOptional<Handler>>::type {
 public:
  using Ptr = std::shared_ptr<StaticPipeline>;

  template <class... HandlerArgs>
  static Ptr create(HandlerArgs&&... handlers) {
    auto ptr =  std::shared_ptr<StaticPipeline>(
        new StaticPipeline(std::forward<HandlerArgs>(handlers)...));
    ptr->initialize();
    return ptr;
  }

  ~StaticPipeline() {
    if (isFirst_) {
      Pipeline<R, W>::detachHandlers();
    }
  }

 protected:
  template <class... HandlerArgs>
  explicit StaticPipeline(HandlerArgs&&... handlers)
    : StaticPipeline(true, std::forward<HandlerArgs>(handlers)...) {
    isFirst_ = true;
  }

  template <class HandlerArg, class... HandlerArgs>
  StaticPipeline(
      bool isFirst,
      HandlerArg&& handler,
      HandlerArgs&&... handlers)
    : StaticPipeline<R, W, Handlers...>(
          false,
          std::forward<HandlerArgs>(handlers)...) {
    isFirst_ = isFirst;
    setHandler(std::forward<HandlerArg>(handler));
    Pipeline<R, W>::addContextFront(&ctx_);
  }

  void initialize() {
    CHECK(handlerPtr_);
    ctx_.initialize(Pipeline<R, W>::shared_from_this(), handlerPtr_);
    StaticPipeline<R, W, Handlers...>::initialize();
  }

 private:
  template <class HandlerArg>
  typename std::enable_if<std::is_same<
    typename std::remove_reference<HandlerArg>::type,
    Handler
  >::value>::type
  setHandler(HandlerArg&& arg) {
    BaseWithOptional<Handler>::handler_.emplace(std::forward<HandlerArg>(arg));
    handlerPtr_ = std::shared_ptr<Handler>(
        &(*BaseWithOptional<Handler>::handler_),
        [](Handler*){});
  }

  template <class HandlerArg>
  typename std::enable_if<std::is_same<
    typename std::decay<HandlerArg>::type,
    std::shared_ptr<Handler>
  >::value>::type
  setHandler(HandlerArg&& arg) {
    handlerPtr_ = std::forward<HandlerArg>(arg);
  }

  template <class HandlerArg>
  typename std::enable_if<std::is_same<
    typename std::decay<HandlerArg>::type,
    Handler*
  >::value>::type
  setHandler(HandlerArg&& arg) {
    handlerPtr_ = std::shared_ptr<Handler>(arg, [](Handler*){});
  }

  bool isFirst_;
  std::shared_ptr<Handler> handlerPtr_;
  typename ContextType<Handler>::type ctx_;
};

} // namespace wangle

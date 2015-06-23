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

#include <folly/io/async/AsyncTransport.h>
#include <folly/futures/Future.h>
#include <folly/ExceptionWrapper.h>

namespace folly { namespace wangle {

class PipelineBase;

template <class In, class Out>
class HandlerContext {
 public:
  virtual ~HandlerContext() {}

  virtual void fireRead(In msg) = 0;
  virtual void fireReadEOF() = 0;
  virtual void fireReadException(exception_wrapper e) = 0;
  virtual void fireTransportActive() = 0;
  virtual void fireTransportInactive() = 0;

  virtual Future<void> fireWrite(Out msg) = 0;
  virtual Future<void> fireClose() = 0;

  virtual PipelineBase* getPipeline() = 0;
  std::shared_ptr<AsyncTransport> getTransport() {
    return getPipeline()->getTransport();
  }

  virtual void setWriteFlags(WriteFlags flags) = 0;
  virtual WriteFlags getWriteFlags() = 0;

  virtual void setReadBufferSettings(
      uint64_t minAvailable,
      uint64_t allocationSize) = 0;
  virtual std::pair<uint64_t, uint64_t> getReadBufferSettings() = 0;

  /* TODO
  template <class H>
  virtual void addHandlerBefore(H&&) {}
  template <class H>
  virtual void addHandlerAfter(H&&) {}
  template <class H>
  virtual void replaceHandler(H&&) {}
  virtual void removeHandler() {}
  */
};

template <class In>
class InboundHandlerContext {
 public:
  virtual ~InboundHandlerContext() {}

  virtual void fireRead(In msg) = 0;
  virtual void fireReadEOF() = 0;
  virtual void fireReadException(exception_wrapper e) = 0;
  virtual void fireTransportActive() = 0;
  virtual void fireTransportInactive() = 0;

  virtual PipelineBase* getPipeline() = 0;
  std::shared_ptr<AsyncTransport> getTransport() {
    return getPipeline()->getTransport();
  }

  // TODO Need get/set writeFlags, readBufferSettings? Probably not.
  // Do we even really need them stored in the pipeline at all?
  // Could just always delegate to the socket impl
};

template <class Out>
class OutboundHandlerContext {
 public:
  virtual ~OutboundHandlerContext() {}

  virtual Future<void> fireWrite(Out msg) = 0;
  virtual Future<void> fireClose() = 0;

  virtual PipelineBase* getPipeline() = 0;
  std::shared_ptr<AsyncTransport> getTransport() {
    return getPipeline()->getTransport();
  }
};

enum class HandlerDir {
  IN,
  OUT,
  BOTH
};

}} // folly::wangle

#include <wangle/channel/HandlerContext-inl.h>

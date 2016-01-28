/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/channel/Pipeline.h>

using folly::WriteFlags;

namespace wangle {

void PipelineBase::setWriteFlags(WriteFlags flags) {
  writeFlags_ = flags;
}

WriteFlags PipelineBase::getWriteFlags() {
  return writeFlags_;
}

void PipelineBase::setReadBufferSettings(
    uint64_t minAvailable,
    uint64_t allocationSize) {
  readBufferSettings_ = std::make_pair(minAvailable, allocationSize);
}

std::pair<uint64_t, uint64_t> PipelineBase::getReadBufferSettings() {
  return readBufferSettings_;
}

void PipelineBase::setTransportInfo(std::shared_ptr<TransportInfo> tInfo) {
  transportInfo_ = tInfo;
}

std::shared_ptr<TransportInfo> PipelineBase::getTransportInfo() {
  return transportInfo_;
}

typename PipelineBase::ContextIterator PipelineBase::removeAt(
    const typename PipelineBase::ContextIterator& it) {
  (*it)->detachPipeline();

  const auto dir = (*it)->getDirection();
  if (dir == HandlerDir::BOTH || dir == HandlerDir::IN) {
    auto it2 = std::find(inCtxs_.begin(), inCtxs_.end(), it->get());
    CHECK(it2 != inCtxs_.end());
    inCtxs_.erase(it2);
  }

  if (dir == HandlerDir::BOTH || dir == HandlerDir::OUT) {
    auto it2 = std::find(outCtxs_.begin(), outCtxs_.end(), it->get());
    CHECK(it2 != outCtxs_.end());
    outCtxs_.erase(it2);
  }

  return ctxs_.erase(it);
}

PipelineBase& PipelineBase::removeFront() {
  if (ctxs_.empty()) {
    throw std::invalid_argument("No handlers in pipeline");
  }
  removeAt(ctxs_.begin());
  return *this;
}

PipelineBase& PipelineBase::removeBack() {
  if (ctxs_.empty()) {
    throw std::invalid_argument("No handlers in pipeline");
  }
  removeAt(--ctxs_.end());
  return *this;
}

void PipelineBase::detachHandlers() {
  for (auto& ctx : ctxs_) {
    if (ctx != owner_) {
      ctx->detachPipeline();
    }
  }
}

} // namespace wangle

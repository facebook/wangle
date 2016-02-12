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

namespace wangle {

template <typename Pipeline, typename R>
void AcceptRoutingHandler<Pipeline, R>::read(Context* ctx,
                                             AcceptPipelineType conn) {
  if (conn.type() != typeid(ConnInfo&)) {
    return;
  }

  populateAcceptors();

  const auto& connInfo = boost::get<ConnInfo&>(conn);
  auto socket = std::shared_ptr<folly::AsyncTransportWrapper>(
      connInfo.sock, folly::DelayedDestruction::Destructor());

  uint64_t connId = nextConnId_++;

  // Create a new routing pipeline for this connection to read from
  // the socket until it parses the routing data
  auto routingPipeline = DefaultPipeline::create();
  routingPipeline->addBack(wangle::AsyncSocketHandler(socket));
  routingPipeline->addBack(routingHandlerFactory_->newHandler(connId, this));
  routingPipeline->finalize();

  // Initialize TransportInfo and set it on the routing pipeline
  auto transportInfo = std::make_shared<TransportInfo>(connInfo.tinfo);
  folly::SocketAddress localAddr, peerAddr;
  socket->getLocalAddress(&localAddr);
  socket->getPeerAddress(&peerAddr);
  transportInfo->localAddr = std::make_shared<folly::SocketAddress>(localAddr);
  transportInfo->remoteAddr = std::make_shared<folly::SocketAddress>(peerAddr);
  routingPipeline->setTransportInfo(transportInfo);

  routingPipeline->transportActive();
  routingPipelines_[connId] = std::move(routingPipeline);
}

template <typename Pipeline, typename R>
void AcceptRoutingHandler<Pipeline, R>::readEOF(Context* ctx) {
  // Null implementation to terminate the call in this handler
}

template <typename Pipeline, typename R>
void AcceptRoutingHandler<Pipeline, R>::readException(
    Context* ctx, folly::exception_wrapper ex) {
  // Null implementation to terminate the call in this handler
}

template <typename Pipeline, typename R>
void AcceptRoutingHandler<Pipeline, R>::onRoutingData(
    uint64_t connId, typename RoutingDataHandler<R>::RoutingData& routingData) {
  // Get the routing pipeline corresponding to this connection
  auto routingPipelineIter = routingPipelines_.find(connId);
  DCHECK(routingPipelineIter != routingPipelines_.end());
  auto routingPipeline = std::move(routingPipelineIter->second);
  routingPipelines_.erase(routingPipelineIter);

  // Fetch the socket from the pipeline and pause reading from the
  // socket
  auto socket = std::dynamic_pointer_cast<folly::AsyncSocket>(
      routingPipeline->getTransport());
  routingPipeline->transportInactive();
  socket->detachEventBase();

  // Hash based on routing data to pick a new acceptor
  uint64_t hash = std::hash<R>()(routingData.routingData);
  auto acceptor = acceptors_[hash % acceptors_.size()];

  // Switch to the new acceptor's thread
  auto mwRoutingData =
      folly::makeMoveWrapper<typename RoutingDataHandler<R>::RoutingData>(
          std::move(routingData));
  acceptor->getEventBase()->runInEventBaseThread([=]() mutable {
    socket->attachEventBase(acceptor->getEventBase());

    auto routingHandler =
        routingPipeline->template getHandler<RoutingDataHandler<R>>();
    DCHECK(routingHandler);
    auto transportInfo = routingPipeline->getTransportInfo();
    auto pipeline = childPipelineFactory_->newPipeline(
        socket, mwRoutingData->routingData, routingHandler, transportInfo);

    auto connection =
        new typename ServerAcceptor<Pipeline>::ServerConnection(pipeline);
    acceptor->addConnection(connection);

    pipeline->transportActive();

    // Pass in the buffered bytes to the pipeline
    pipeline->read(mwRoutingData->bufQueue);
  });
}

template <typename Pipeline, typename R>
void AcceptRoutingHandler<Pipeline, R>::onError(uint64_t connId,
                                                folly::exception_wrapper ex) {
  VLOG(4) << "Exception while parsing routing data: " << ex.what();

  // Notify all handlers of the exception
  auto ctx = getContext();
  auto pipeline =
      CHECK_NOTNULL(dynamic_cast<AcceptPipeline*>(ctx->getPipeline()));
  pipeline->readException(ex);

  // Delete the routing pipeline. This will close and delete the socket as well.
  routingPipelines_.erase(connId);
}

template <typename Pipeline, typename R>
void AcceptRoutingHandler<Pipeline, R>::populateAcceptors() {
  if (!acceptors_.empty()) {
    return;
  }
  CHECK(server_);
  server_->forEachWorker(
      [&](Acceptor* acceptor) { acceptors_.push_back(acceptor); });
}

} // namespace wangle

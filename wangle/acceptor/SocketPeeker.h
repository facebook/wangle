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

#include <array>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/AsyncSocket.h>

namespace wangle {

template <size_t N>
class SocketPeeker : public folly::AsyncTransportWrapper::ReadCallback,
                     public folly::DelayedDestruction {
 public:
  using UniquePtr =
      std::unique_ptr<SocketPeeker, folly::DelayedDestruction::Destructor>;

  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void peekSuccess(std::array<uint8_t, N> data) noexcept = 0;
    virtual void peekError(const folly::AsyncSocketException& ex) noexcept = 0;
  };

  SocketPeeker(folly::AsyncSocket& socket, Callback* callback)
      : socket_(socket), callback_(callback) {}

  void start() {
    socket_.setPeek(true);
    socket_.setReadCB(this);
  }

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = reinterpret_cast<void*>(peekBytes_.data());
    *lenReturn = N;
  }

  void readEOF() noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);

    auto type =
        folly::AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE;
    readErr(folly::AsyncSocketException(type, "Unexpected EOF"));
  }

  void readErr(const folly::AsyncSocketException& ex) noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);

    unsetPeek();
    if (callback_) {
      auto callback = callback_;
      callback_ = nullptr;
      callback->peekError(ex);
    }
  }

  void readDataAvailable(size_t len) noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);

    // Peek does not advance the socket buffer, so we will
    // always re-read the existing bytes, so we should only
    // consider it a successful peek if we read all N bytes.
    if (len != N) {
      return;
    }
    unsetPeek();
    auto callback = callback_;
    callback_ = nullptr;
    callback->peekSuccess(std::move(peekBytes_));
  }

  bool isBufferMovable() noexcept override {
    // Returning false so that we can supply the exact length of the
    // number of bytes we want to read.
    return false;
  }

 protected:
  void unsetPeek() {
    socket_.setPeek(false);
    socket_.setReadCB(nullptr);
  }

  ~SocketPeeker() {
    if (socket_.getReadCallback() == this) {
      unsetPeek();
    }
  }

 private:
  folly::AsyncSocket& socket_;
  Callback* callback_;
  std::array<uint8_t, N> peekBytes_;
};
}

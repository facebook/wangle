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
#pragma once

#include <array>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/AsyncSocket.h>

namespace wangle {

class SocketPeeker : public folly::AsyncTransportWrapper::ReadCallback,
                     public folly::DelayedDestruction {
 public:
  using UniquePtr =
      std::unique_ptr<SocketPeeker, folly::DelayedDestruction::Destructor>;

  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void peekSuccess(std::vector<uint8_t> data) noexcept = 0;
    virtual void peekError(const folly::AsyncSocketException& ex) noexcept = 0;
  };

  SocketPeeker(folly::AsyncSocket& socket, Callback* callback, size_t numBytes)
      : socket_(socket), callback_(callback), peekBytes_(numBytes) {}

  ~SocketPeeker() override {
    if (socket_.getReadCallback() == this) {
      socket_.setReadCB(nullptr);
    }
  }

  void start() {
    if (peekBytes_.size() == 0) {
      // No peeking necessary.
      auto callback = callback_;
      callback_ = nullptr;
      callback->peekSuccess(std::move(peekBytes_));
    } else {
      socket_.setReadCB(this);
    }
  }

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    CHECK_LT(read_, peekBytes_.size());
    *bufReturn = reinterpret_cast<void*>(peekBytes_.data() + read_);
    *lenReturn = peekBytes_.size() - read_;
  }

  void readEOF() noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);

    auto type =
        folly::AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE;
    readErr(folly::AsyncSocketException(type, "Unexpected EOF"));
  }

  void readErr(const folly::AsyncSocketException& ex) noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);

    socket_.setReadCB(nullptr);
    if (callback_) {
      auto callback = callback_;
      callback_ = nullptr;
      callback->peekError(ex);
    }
  }

  void readDataAvailable(size_t len) noexcept override {
    folly::DelayedDestruction::DestructorGuard dg(this);

    read_ += len;
    CHECK_LE(read_, peekBytes_.size());

    if (read_ == peekBytes_.size()) {
      socket_.setPreReceivedData(
          folly::IOBuf::copyBuffer(folly::range(peekBytes_)));
      socket_.setReadCB(nullptr);
      auto callback = callback_;
      callback_ = nullptr;
      callback->peekSuccess(std::move(peekBytes_));
    }
  }

  bool isBufferMovable() noexcept override {
    // Returning false so that we can supply the exact length of the
    // number of bytes we want to read.
    return false;
  }

 private:
  folly::AsyncSocket& socket_;
  Callback* callback_;
  size_t read_{0};
  std::vector<uint8_t> peekBytes_;
};
}

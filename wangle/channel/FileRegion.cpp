/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/channel/FileRegion.h>

#ifdef SPLICE_F_NONBLOCK
using namespace folly;
using namespace wangle;

namespace {

struct FileRegionReadPool {};

Singleton<IOThreadPoolExecutor, FileRegionReadPool> readPool(
  []{
    return new IOThreadPoolExecutor(
        sysconf(_SC_NPROCESSORS_ONLN),
        std::make_shared<NamedThreadFactory>("FileRegionReadPool"));
  });

}

namespace wangle {

FileRegion::FileWriteRequest::FileWriteRequest(AsyncSocket* socket,
    WriteCallback* callback, int fd, off_t offset, size_t count)
  : WriteRequest(socket, callback),
    readFd_(fd), offset_(offset), count_(count) {
}

void FileRegion::FileWriteRequest::destroy() {
  readBase_->runInEventBaseThread([this]{
    delete this;
  });
}

AsyncSocket::WriteResult FileRegion::FileWriteRequest::performWrite() {
  if (!started_) {
    start();
    return AsyncSocket::WriteResult(0);
  }

  int flags = SPLICE_F_NONBLOCK | SPLICE_F_MORE;
  ssize_t spliced = ::splice(pipe_out_, nullptr,
                             socket_->getFd(), nullptr,
                             bytesInPipe_, flags);
  if (spliced == -1) {
    if (errno == EAGAIN) {
      return AsyncSocket::WriteResult(0);
    }
    return AsyncSocket::WriteResult(-1);
  }

  bytesInPipe_ -= spliced;
  bytesWritten(spliced);
  return AsyncSocket::WriteResult(spliced);
}

void FileRegion::FileWriteRequest::consume() {
  // do nothing
}

bool FileRegion::FileWriteRequest::isComplete() {
  return totalBytesWritten_ == count_;
}

void FileRegion::FileWriteRequest::messageAvailable(size_t&& count) {
  bool shouldWrite = bytesInPipe_ == 0;
  bytesInPipe_ += count;
  if (shouldWrite) {
    socket_->writeRequestReady();
  }
}

#ifdef __GLIBC__
# if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 9))
#   define GLIBC_AT_LEAST_2_9 1
#  endif
#endif

void FileRegion::FileWriteRequest::start() {
  started_ = true;
  readBase_ = readPool.try_get()->getEventBase();
  readBase_->runInEventBaseThread([this]{
    auto flags = fcntl(readFd_, F_GETFL);
    if (flags == -1) {
      fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "fcntl F_GETFL failed", errno));
      return;
    }

    flags &= O_ACCMODE;
    if (flags == O_WRONLY) {
      fail(__func__, AsyncSocketException(
          AsyncSocketException::BAD_ARGS, "file not open for reading"));
      return;
    }

#ifndef GLIBC_AT_LEAST_2_9
    fail(__func__, AsyncSocketException(
        AsyncSocketException::NOT_SUPPORTED,
        "writeFile unsupported on glibc < 2.9"));
    return;
#else
    int pipeFds[2];
    if (::pipe2(pipeFds, O_NONBLOCK) == -1) {
      fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "pipe2 failed", errno));
      return;
    }

#ifdef F_SETPIPE_SZ
    // Max size for unprevileged processes as set in /proc/sys/fs/pipe-max-size
    // Ignore failures and just roll with it
    // TODO maybe read max size from /proc?
    fcntl(pipeFds[0], F_SETPIPE_SZ, 1048576);
    fcntl(pipeFds[1], F_SETPIPE_SZ, 1048576);
#endif

    pipe_out_ = pipeFds[0];

    socket_->getEventBase()->runInEventBaseThreadAndWait([&]{
      startConsuming(socket_->getEventBase(), &queue_);
    });
    readHandler_ = folly::make_unique<FileReadHandler>(
        this, pipeFds[1], count_);
#endif
  });
}

FileRegion::FileWriteRequest::~FileWriteRequest() {
  CHECK(readBase_->isInEventBaseThread());
  socket_->getEventBase()->runInEventBaseThreadAndWait([&]{
    stopConsuming();
    if (pipe_out_ > -1) {
      ::close(pipe_out_);
    }
  });

}

void FileRegion::FileWriteRequest::fail(
    const char* fn,
    const AsyncSocketException& ex) {
  socket_->getEventBase()->runInEventBaseThread([=]{
    WriteRequest::fail(fn, ex);
  });
}

FileRegion::FileWriteRequest::FileReadHandler::FileReadHandler(
    FileWriteRequest* req, int pipe_in, size_t bytesToRead)
  : req_(req), pipe_in_(pipe_in), bytesToRead_(bytesToRead) {
  CHECK(req_->readBase_->isInEventBaseThread());
  initHandler(req_->readBase_, pipe_in);
  if (!registerHandler(EventFlags::WRITE | EventFlags::PERSIST)) {
    req_->fail(__func__, AsyncSocketException(
        AsyncSocketException::INTERNAL_ERROR,
        "registerHandler failed"));
  }
}

FileRegion::FileWriteRequest::FileReadHandler::~FileReadHandler() {
  CHECK(req_->readBase_->isInEventBaseThread());
  unregisterHandler();
  ::close(pipe_in_);
}

void FileRegion::FileWriteRequest::FileReadHandler::handlerReady(
    uint16_t events) noexcept {
  CHECK(events & EventHandler::WRITE);
  if (bytesToRead_ == 0) {
    unregisterHandler();
    return;
  }

  int flags = SPLICE_F_NONBLOCK | SPLICE_F_MORE;
  ssize_t spliced = ::splice(req_->readFd_, &req_->offset_,
                             pipe_in_, nullptr,
                             bytesToRead_, flags);
  if (spliced == -1) {
    if (errno == EAGAIN) {
      return;
    } else {
      req_->fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "splice failed", errno));
      return;
    }
  }

  if (spliced > 0) {
    bytesToRead_ -= spliced;
    try {
      req_->queue_.putMessage(static_cast<size_t>(spliced));
    } catch (...) {
      req_->fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "putMessage failed"));
      return;
    }
  }
}

} // wangle
#endif

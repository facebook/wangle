/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/ssl/TLSCredProcessor.h>

#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>

using namespace folly;

namespace {

constexpr std::chrono::milliseconds kTicketPollInterval =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(10));

void insertSeeds(const folly::dynamic& keyConfig,
                 std::vector<std::string>& seedList) {
  if (!keyConfig.isArray()) {
    return;
  }
  for (const auto& seed : keyConfig) {
    seedList.push_back(seed.asString());
  }
}
}

namespace wangle {

TLSCredProcessor::TLSCredProcessor()
    : poller_(std::make_unique<FilePoller>(kTicketPollInterval)) {}

TLSCredProcessor::TLSCredProcessor(const std::string& ticketFile,
                                   const std::string& certFile)
    : TLSCredProcessor() {
  setTicketPathToWatch(ticketFile);
  setCertPathToWatch(certFile);
}

void TLSCredProcessor::stop() {
  poller_->stop();
}

TLSCredProcessor::~TLSCredProcessor() { stop(); }

void TLSCredProcessor::addTicketCallback(
    std::function<void(TLSTicketKeySeeds)> callback) {
  ticketCallbacks_.push_back(std::move(callback));
}

void TLSCredProcessor::addCertCallback(
    std::function<void()> callback) {
  certCallbacks_.push_back(std::move(callback));
}

void TLSCredProcessor::setTicketPathToWatch(const std::string& ticketFile) {
  if (!ticketFile_.empty()) {
    poller_->removeFileToTrack(ticketFile_);
  }
  ticketFile_ = ticketFile;
  if (!ticketFile_.empty()) {
    auto ticketsChangedCob = [=]() { ticketFileUpdated(ticketFile); };
    poller_->addFileToTrack(ticketFile_, ticketsChangedCob);
  }
}

void TLSCredProcessor::setCertPathToWatch(const std::string& certFile) {
  if (!certFile_.empty()) {
    poller_->removeFileToTrack(certFile_);
  }
  certFile_ = certFile;
  if (!certFile_.empty()) {
    auto certChangedCob = [this]() { certFileUpdated(); };
    poller_->addFileToTrack(certFile_, certChangedCob);
  }
}

void TLSCredProcessor::ticketFileUpdated(
    const std::string& ticketFile) noexcept {
  auto seeds = processTLSTickets(ticketFile);
  if (seeds) {
    for (auto& callback : ticketCallbacks_) {
      callback(*seeds);
    }
  }
}

void TLSCredProcessor::certFileUpdated() noexcept {
  for (const auto& callback: certCallbacks_) {
    callback();
  }
}

/* static */ Optional<TLSTicketKeySeeds> TLSCredProcessor::processTLSTickets(
    const std::string& fileName) {
  try {
    std::string jsonData;
    folly::readFile(fileName.c_str(), jsonData);
    folly::dynamic conf = folly::parseJson(jsonData);
    if (conf.type() != dynamic::Type::OBJECT) {
      LOG(ERROR) << "Error parsing " << fileName << " expected object";
      return Optional<TLSTicketKeySeeds>();
    }
    TLSTicketKeySeeds seedData;
    if (conf.count("old")) {
      insertSeeds(conf["old"], seedData.oldSeeds);
    }
    if (conf.count("current")) {
      insertSeeds(conf["current"], seedData.currentSeeds);
    }
    if (conf.count("new")) {
      insertSeeds(conf["new"], seedData.newSeeds);
    }
    return seedData;
  } catch (const std::exception& ex) {
    LOG(ERROR) << "parsing " << fileName << " " << ex.what();
    return Optional<TLSTicketKeySeeds>();
  }
}
}

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
#include <wangle/ssl/TLSCredProcessor.h>

#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>

using namespace folly;

namespace {

constexpr std::chrono::milliseconds kCredentialPollInterval =
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
    : poller_(std::make_unique<FilePoller>(kCredentialPollInterval)) {}

TLSCredProcessor::TLSCredProcessor(std::chrono::milliseconds pollInterval)
    : poller_(std::make_unique<FilePoller>(pollInterval)) {}

void TLSCredProcessor::stop() {
  poller_->stop();
}

TLSCredProcessor::~TLSCredProcessor() { stop(); }

void TLSCredProcessor::setPollInterval(std::chrono::milliseconds pollInterval) {
  poller_->stop();
  poller_ = std::make_unique<FilePoller>(pollInterval);
  setTicketPathToWatch(ticketFile_);
  setCertPathsToWatch(certFiles_);
}

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

void TLSCredProcessor::setCertPathsToWatch(std::set<std::string> certFiles) {
  for (const auto& path: certFiles_) {
    poller_->removeFileToTrack(path);
  }
  certFiles_ = std::move(certFiles);
  if (!certFiles_.empty()) {
    auto certChangedCob = [this]() { certFileUpdated(); };
    for (const auto& path: certFiles_) {
      poller_->addFileToTrack(path, certChangedCob);
    }
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
    if (!folly::readFile(fileName.c_str(), jsonData)) {
      LOG(WARNING) << "Failed to read " << fileName
                   << "; Ticket seeds are unavailable.";
      return folly::none;
    }
    folly::dynamic conf = folly::parseJson(jsonData);
    if (conf.type() != dynamic::Type::OBJECT) {
      LOG(WARNING) << "Error parsing " << fileName << " expected object";
      return folly::none;
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
    LOG(WARNING) << "Parsing " << fileName << " failed: " << ex.what();
    return folly::none;
  }
}

}

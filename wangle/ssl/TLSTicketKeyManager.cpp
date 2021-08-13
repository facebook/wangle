/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/ssl/TLSTicketKeyManager.h>

#include <folly/GLog.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/portability/OpenSSL.h>
#include <openssl/aes.h>
#include <wangle/ssl/SSLStats.h>
#include <wangle/ssl/SSLUtil.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

namespace {

const int kTLSTicketKeyNameLen = 4;
const int kTLSTicketKeySaltLen = 12;

} // namespace

namespace wangle {

std::unique_ptr<TLSTicketKeyManager> TLSTicketKeyManager::fromSeeds(
    const TLSTicketKeySeeds* seeds) {
  auto mgr = std::make_unique<TLSTicketKeyManager>();
  mgr->setTLSTicketKeySeeds(
      seeds->oldSeeds, seeds->currentSeeds, seeds->newSeeds);
  return mgr;
}

TLSTicketKeyManager::TLSTicketKeyManager() {}

TLSTicketKeyManager::~TLSTicketKeyManager() {}

int TLSTicketKeyManager::ticketCallback(
    SSL*,
    unsigned char* keyName,
    unsigned char* iv,
    EVP_CIPHER_CTX* cipherCtx,
    HMAC_CTX* hmacCtx,
    int encrypt) {
  int result = 0;

  if (encrypt) {
    result = encryptCallback(keyName, iv, cipherCtx, hmacCtx);
    // recordTLSTicket() below will unconditionally increment the new ticket
    // counter regardless of result value, so exit early here.
    if (result == 0) {
      return result;
    }
  } else {
    result = decryptCallback(keyName, iv, cipherCtx, hmacCtx);
  }

  // Result records whether a ticket key was found to encrypt or decrypt this
  // ticket, not whether the session was re-used.
  if (stats_) {
    stats_->recordTLSTicket(encrypt, result);
  }

  return result;
}

int TLSTicketKeyManager::encryptCallback(
    unsigned char* keyName,
    unsigned char* iv,
    EVP_CIPHER_CTX* cipherCtx,
    HMAC_CTX* hmacCtx) {
  uint8_t salt[kTLSTicketKeySaltLen];
  uint8_t output[SHA256_DIGEST_LENGTH];
  uint8_t* hmacKey = nullptr;
  uint8_t* aesKey = nullptr;

  auto key = findEncryptionKey();
  if (key == nullptr) {
    // no keys available to encrypt
    FB_LOG_EVERY_MS(ERROR, 1000)
        << "No TLS ticket key available for encryption. Either set a ticket "
        << "key or uninstall TLSTicketKeyManager from this SSLContext.";
    return 0;
  }
  VLOG(4) << "Encrypting new ticket with key name="
          << SSLUtil::hexlify(key->keyName_);

  // Get a random salt and write out key name
  if (RAND_bytes(salt, (int)sizeof(salt)) != 1 &&
      ERR_GET_LIB(ERR_peek_error()) == ERR_LIB_RAND) {
    ERR_get_error();
  }
  memcpy(keyName, key->keyName_.data(), kTLSTicketKeyNameLen);
  memcpy(keyName + kTLSTicketKeyNameLen, salt, kTLSTicketKeySaltLen);

  // Create the unique keys by hashing with the salt
  makeUniqueKeys(key->keySource_, sizeof(key->keySource_), salt, output);
  // This relies on the fact that SHA256 has 32 bytes of output
  // and that AES-128 keys are 16 bytes
  hmacKey = output;
  aesKey = output + SHA256_DIGEST_LENGTH / 2;

  // Initialize iv and cipher/mac CTX
  if (RAND_bytes(iv, AES_BLOCK_SIZE) != 1 &&
      ERR_GET_LIB(ERR_peek_error()) == ERR_LIB_RAND) {
    ERR_get_error();
  }
  HMAC_Init_ex(
      hmacCtx, hmacKey, SHA256_DIGEST_LENGTH / 2, EVP_sha256(), nullptr);
  EVP_EncryptInit_ex(cipherCtx, EVP_aes_128_cbc(), nullptr, aesKey, iv);

  return 1;
}

int TLSTicketKeyManager::decryptCallback(
    unsigned char* keyName,
    unsigned char* iv,
    EVP_CIPHER_CTX* cipherCtx,
    HMAC_CTX* hmacCtx) {
  uint8_t* saltptr = nullptr;
  uint8_t output[SHA256_DIGEST_LENGTH];
  uint8_t* hmacKey = nullptr;
  uint8_t* aesKey = nullptr;

  auto key = findDecryptionKey(keyName);
  if (key == nullptr) {
    // no ticket found for decryption - will issue a new ticket
    std::string skeyName((char*)keyName, kTLSTicketKeyNameLen);
    VLOG(4) << "Can't find ticket key with name=" << SSLUtil::hexlify(skeyName)
            << ", will generate new ticket";
    return 0;
  }
  VLOG(4) << "Decrypting ticket with key name="
          << SSLUtil::hexlify(key->keyName_);

  // Reconstruct the unique key via the salt
  saltptr = keyName + kTLSTicketKeyNameLen;
  makeUniqueKeys(key->keySource_, sizeof(key->keySource_), saltptr, output);
  hmacKey = output;
  aesKey = output + SHA256_DIGEST_LENGTH / 2;

  // Initialize cipher/mac CTX
  HMAC_Init_ex(
      hmacCtx, hmacKey, SHA256_DIGEST_LENGTH / 2, EVP_sha256(), nullptr);
  EVP_DecryptInit_ex(cipherCtx, EVP_aes_128_cbc(), nullptr, aesKey, iv);

  return 1;
}

bool TLSTicketKeyManager::setTLSTicketKeySeeds(
    const std::vector<std::string>& oldSeeds,
    const std::vector<std::string>& currentSeeds,
    const std::vector<std::string>& newSeeds) {
  recordTlsTicketRotation(oldSeeds, currentSeeds, newSeeds);

  bool result = true;

  activeKeys_.clear();
  ticketKeys_.clear();
  ticketSeeds_.clear();
  const std::vector<std::string>* seedList = &oldSeeds;
  for (uint32_t i = 0; i < 3; i++) {
    TLSTicketSeedType type = (TLSTicketSeedType)i;
    if (type == SEED_CURRENT) {
      seedList = &currentSeeds;
    } else if (type == SEED_NEW) {
      seedList = &newSeeds;
    }

    for (const auto& seedInput : *seedList) {
      TLSTicketSeed* seed = insertSeed(seedInput, type);
      if (seed == nullptr) {
        result = false;
        continue;
      }
      insertNewKey(seed, 1, nullptr);
    }
  }
  if (!result) {
    VLOG(2) << "One or more seeds failed to decode";
  }

  if (ticketKeys_.size() == 0 || activeKeys_.size() == 0) {
    VLOG(1) << "No keys configured, session ticket resumption disabled";
    return false;
  }

  return true;
}

bool TLSTicketKeyManager::getTLSTicketKeySeeds(
    std::vector<std::string>& oldSeeds,
    std::vector<std::string>& currentSeeds,
    std::vector<std::string>& newSeeds) const {
  oldSeeds.clear();
  currentSeeds.clear();
  newSeeds.clear();
  bool allGot = true;
  for (const auto& seed : ticketSeeds_) {
    std::string hexSeed;
    if (!folly::hexlify(seed->seed_, hexSeed)) {
      allGot = false;
      continue;
    }
    if (seed->type_ == TLSTicketSeedType::SEED_OLD) {
      oldSeeds.push_back(hexSeed);
    } else if (seed->type_ == TLSTicketSeedType::SEED_CURRENT) {
      currentSeeds.push_back(hexSeed);
    } else {
      newSeeds.push_back(hexSeed);
    }
  }
  return allGot;
}

void TLSTicketKeyManager::recordTlsTicketRotation(
    const std::vector<std::string>& oldSeeds,
    const std::vector<std::string>& currentSeeds,
    const std::vector<std::string>& newSeeds) {
  if (stats_) {
    TLSTicketKeySeeds next{oldSeeds, currentSeeds, newSeeds};
    TLSTicketKeySeeds current;
    getTLSTicketKeySeeds(
        current.oldSeeds, current.currentSeeds, current.newSeeds);
    stats_->recordTLSTicketRotation(current.isValidRotation(next));
  }
}

std::string TLSTicketKeyManager::makeKeyName(
    TLSTicketSeed* seed,
    uint32_t n,
    unsigned char* nameBuf) {
  SHA256_CTX ctx;

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, seed->seedName_, sizeof(seed->seedName_));
  SHA256_Update(&ctx, &n, sizeof(n));
  SHA256_Final(nameBuf, &ctx);
  return std::string((char*)nameBuf, kTLSTicketKeyNameLen);
}

TLSTicketKeyManager::TLSTicketKeySource* TLSTicketKeyManager::insertNewKey(
    TLSTicketSeed* seed,
    uint32_t hashCount,
    TLSTicketKeySource* prevKey) {
  unsigned char nameBuf[SHA256_DIGEST_LENGTH];
  std::unique_ptr<TLSTicketKeySource> newKey(new TLSTicketKeySource);

  // This function supports hash chaining but it is not currently used.

  if (prevKey != nullptr) {
    hashNth(
        prevKey->keySource_,
        sizeof(prevKey->keySource_),
        newKey->keySource_,
        1);
  } else {
    // can't go backwards or the current is missing, start from the beginning
    hashNth(
        (unsigned char*)seed->seed_.data(),
        seed->seed_.length(),
        newKey->keySource_,
        hashCount);
  }

  newKey->hashCount_ = hashCount;
  newKey->keyName_ = makeKeyName(seed, hashCount, nameBuf);
  newKey->type_ = seed->type_;
  auto newKeyName = newKey->keyName_;
  auto it = ticketKeys_.insert(
      std::make_pair(std::move(newKeyName), std::move(newKey)));

  auto key = it.first->second.get();
  if (key->type_ == SEED_CURRENT) {
    activeKeys_.push_back(key);
  }
  VLOG(4) << "Adding key for " << hashCount << " type=" << (uint32_t)key->type_
          << " Name=" << SSLUtil::hexlify(key->keyName_);

  return key;
}

void TLSTicketKeyManager::hashNth(
    const unsigned char* input,
    size_t input_len,
    unsigned char* output,
    uint32_t n) {
  assert(n > 0);
  for (uint32_t i = 0; i < n; i++) {
    SHA256(input, input_len, output);
    input = output;
    input_len = SHA256_DIGEST_LENGTH;
  }
}

TLSTicketKeyManager::TLSTicketSeed* TLSTicketKeyManager::insertSeed(
    const std::string& seedInput,
    TLSTicketSeedType type) {
  TLSTicketSeed* seed = nullptr;
  std::string seedOutput;

  if (!folly::unhexlify<std::string, std::string>(seedInput, seedOutput)) {
    LOG(WARNING) << "Failed to decode seed type=" << (uint32_t)type
                 << " seed=" << seedInput;
    return seed;
  }

  seed = new TLSTicketSeed();
  seed->seed_ = seedOutput;
  seed->type_ = type;
  SHA256(
      (unsigned char*)seedOutput.data(), seedOutput.length(), seed->seedName_);
  ticketSeeds_.push_back(std::unique_ptr<TLSTicketSeed>(seed));

  return seed;
}

TLSTicketKeyManager::TLSTicketKeySource*
TLSTicketKeyManager::findEncryptionKey() {
  TLSTicketKeySource* result = nullptr;
  // call to rand here is a bit hokey since it's not cryptographically
  // random, and is predictably seeded with 0.  However, activeKeys_
  // is probably not going to have very many keys in it, and most
  // likely only 1.
  size_t numKeys = activeKeys_.size();
  if (numKeys > 0) {
    auto const i = numKeys == 1 ? 0ul : folly::Random::rand64(numKeys);
    result = activeKeys_[i];
  }
  return result;
}

TLSTicketKeyManager::TLSTicketKeySource* TLSTicketKeyManager::findDecryptionKey(
    unsigned char* keyName) {
  std::string name((char*)keyName, kTLSTicketKeyNameLen);
  TLSTicketKeySource* key = nullptr;
  TLSTicketKeyMap::iterator mapit = ticketKeys_.find(name);
  if (mapit != ticketKeys_.end()) {
    key = mapit->second.get();
  }
  return key;
}

void TLSTicketKeyManager::makeUniqueKeys(
    unsigned char* parentKey,
    size_t keyLen,
    unsigned char* salt,
    unsigned char* output) {
  SHA256_CTX hash_ctx;

  SHA256_Init(&hash_ctx);
  SHA256_Update(&hash_ctx, parentKey, keyLen);
  SHA256_Update(&hash_ctx, salt, kTLSTicketKeySaltLen);
  SHA256_Final(output, &hash_ctx);
}

} // namespace wangle

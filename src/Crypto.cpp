#include "Crypto.hpp"
#include "external/monocypher.h"
#include <Geode/utils/random.hpp>
#include <Geode/utils/crypto.hpp>
#include <cstring>

using namespace geode::prelude;

namespace argon {

static void fillBytesSecure(uint8_t* buf, size_t size) {
    while (size > 0) {
        auto u64 = geode::utils::random::secureU64();
        auto toCopy = std::min(size, sizeof(u64));
        std::memcpy(buf, &u64, toCopy);
        buf += toCopy;
        size -= toCopy;
    }
}

Key createKey() {
    // Note: the key is not meant to be private nor very secure,
    // this is just to prevent token hijacking in case some malware reads all files on the computer
    auto hash = geode::sha256("terribly insecure argon private key");
    return Key { hash.data };
}

std::vector<uint8_t> encrypt(std::span<const uint8_t> data, const Key& key) {
    uint8_t nonce[24] = {0};
    uint8_t mac[16] = {0};

    fillBytesSecure(nonce, sizeof(nonce));

    std::vector<uint8_t> output;
    output.resize(data.size() + sizeof(mac) + sizeof(nonce));

    crypto_aead_lock(output.data(), mac, key.data.data(), nonce, nullptr, 0, data.data(), data.size());

    // append mac + nonce
    size_t dataEnd = data.size();
    std::memcpy(output.data() + dataEnd, mac, sizeof(mac));
    std::memcpy(output.data() + dataEnd + sizeof(mac), nonce, sizeof(nonce));
    return output;
}

Result<std::vector<uint8_t>> decrypt(std::span<const uint8_t> data, const Key& key) {
    if (data.size() < 40) {
        return Err("data is too short");
    }

    size_t dataEnd = data.size() - 40;
    std::vector<uint8_t> output;
    output.resize(dataEnd);

    uint8_t mac[16] = {0};
    uint8_t nonce[24] = {0};

    std::memcpy(mac, data.data() + dataEnd, sizeof(mac));
    std::memcpy(nonce, data.data() + dataEnd + sizeof(mac), sizeof(nonce));

    int result = crypto_aead_unlock(output.data(), mac, key.data.data(), nonce, nullptr, 0, data.data(), dataEnd);
    if (result != 0) {
        return Err("decryption failed");
    }

    return Ok(std::move(output));
}

}

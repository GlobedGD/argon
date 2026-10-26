#include "ArgonStorage.hpp"
#include "Crypto.hpp"

#include <Geode/loader/Dirs.hpp>
#include <Geode/utils/file.hpp>
#include <matjson.hpp>
#include <asp/fs.hpp>

using namespace geode::prelude;

static auto g_storagePath = Mod::get()->getSaveDir() / ".data.bin";

namespace argon {

static bool expired(uint64_t expiry) {
    uint64_t now = asp::SystemTime::now().to_time_t();

    if (expiry < 30) return true;

    return now >= (expiry - 30); // 30s buffer
}

static bool mismatch(const AccountData& account, const AccountEntry& entry) {
    return account.accountId != entry.accountId || account.userId != entry.userId || account.username != entry.username;
}

ArgonStorage::ArgonStorage() {
    if (auto err = this->load().err()) {
        log::warn("Failed to load storage: {}", *err);
    }
}

std::optional<std::string> ArgonStorage::getAccessToken(std::string_view serverUrl, const AccountData& account) {
    auto data = m_data.lock();
    auto it = data->servers.find(serverUrl);
    if (it == data->servers.end()) {
        return std::nullopt;
    }

    auto& serverData = it->second;
    auto accIt = serverData.accounts.find(account.accountId);
    if (accIt == serverData.accounts.end()) {
        return std::nullopt;
    }

    auto& accEntry = accIt->second;
    if (expired(accEntry.accessTokenExpiry) || mismatch(account, accEntry)) {
        log::debug("not fetching access token because it expired or data is mismatched!");
        return std::nullopt;
    }

    return accEntry.accessToken;
}

std::optional<std::string> ArgonStorage::getRefreshToken(std::string_view serverUrl, const AccountData& account) {
    auto data = m_data.lock();
    auto it = data->servers.find(serverUrl);
    if (it == data->servers.end()) {
        return std::nullopt;
    }

    auto& serverData = it->second;
    auto accIt = serverData.accounts.find(account.accountId);
    if (accIt == serverData.accounts.end()) {
        return std::nullopt;
    }

    auto& accEntry = accIt->second;
    if (expired(accEntry.refreshTokenExpiry) || mismatch(account, accEntry)) {
        log::debug("not fetching refresh token because it expired or data is mismatched!");
        return std::nullopt;
    }

    return accEntry.refreshToken;
}

void ArgonStorage::storeRefreshToken(std::string_view serverUrl, const AccountData& account, std::string refreshToken, uint64_t expiry) {
    auto data = m_data.lock();
    auto& serverData = this->serverData(data, serverUrl);
    auto& accEntry = serverData.accounts[account.accountId];

    accEntry.accountId = account.accountId;
    accEntry.userId = account.userId;
    accEntry.username = account.username;
    accEntry.refreshToken = std::move(refreshToken);
    accEntry.refreshTokenExpiry = expiry;

    if (auto err = this->save().err()) {
        log::warn("Failed to save storage: {}", *err);
    }
}

void ArgonStorage::storeAccessToken(std::string_view serverUrl, const AccountData& account, std::string accessToken, uint64_t expiry) {
    auto data = m_data.lock();
    auto& serverData = this->serverData(data, serverUrl);
    auto& accEntry = serverData.accounts[account.accountId];

    accEntry.accountId = account.accountId;
    accEntry.userId = account.userId;
    accEntry.username = account.username;
    accEntry.accessToken = std::move(accessToken);
    accEntry.accessTokenExpiry = expiry;

    if (auto err = this->save().err()) {
        log::warn("Failed to save storage: {}", *err);
    }
}

void ArgonStorage::clearAllTokens() {
    m_data.lock()->servers.clear();

    if (auto err = this->save().err()) {
        log::warn("Failed to save storage: {}", *err);
    }
}

void ArgonStorage::clearTokens(int accountId) {
    auto data = m_data.lock();
    for (auto& [_, serverData] : data->servers) {
        serverData.accounts.erase(accountId);
    }

    if (auto err = this->save().err()) {
        log::warn("Failed to save storage: {}", *err);
    }
}

bool ArgonStorage::hasToken(const AccountData& account, std::string_view serverUrl) {
    return this->getAccessToken(serverUrl, account).has_value();
}

StorageData::ServerData& ArgonStorage::serverData(decltype(m_data)::Guard& guard, std::string_view serverUrl) {
    auto it = guard->servers.find(serverUrl);
    if (it == guard->servers.end()) {
        auto [newIt, _] = guard->servers.emplace(serverUrl, StorageData::ServerData{});
        return newIt->second;
    }
    return it->second;
}

Result<> ArgonStorage::load() {
    auto result = asp::fs::read(g_storagePath);
    if (!result && result.unwrapErr() == asp::fs::ErrorKind::NotFound) {
        return Ok();
    }

    auto data = GEODE_UNWRAP(result);
    auto key = createKey();
    auto plaintext = GEODE_UNWRAP(decrypt(data, key));

    auto jsonResult = glz::read_json<StorageData>(plaintext);
    if (!jsonResult) {
        return Err("Failed to parse storage data: {}", jsonResult.error().custom_error_message);
    }

    *m_data.lock() = std::move(jsonResult).value();

    return Ok();
}

Result<> ArgonStorage::save() {
    auto key = createKey();

    std::vector<uint8_t> buffer;
    if (glz::write_json(*m_data.lock(), buffer)) {
        return Err("failed to write json");
    }

    auto ciphertext = encrypt(buffer, key);
    auto result = asp::fs::write(g_storagePath, ciphertext);
    if (!result) {
        return Err("Failed to write storage data: {}", result.unwrapErr());
    }

    return Ok();
}

} // namespace argon

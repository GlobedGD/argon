#pragma once
#include "util.hpp"
#include <Geode/utils/StringMap.hpp>
#include <argon.hpp>
#include <glaze/glaze.hpp>
#include <asp/sync/Mutex.hpp>

namespace argon {

struct Token {
    std::string token;
    asp::SystemTime expiry;

    bool expired() {
        // 30s buffer
        return expiry.until() < asp::Duration::fromSecs(30);
    }
};

struct AccountEntry {
    int32_t accountId;
    int32_t userId;
    std::string username;
    std::string refreshToken;
    uint64_t refreshTokenExpiry;
    std::string accessToken;
    uint64_t accessTokenExpiry;

    Token getRefreshToken() const {
        return Token{
            .token = refreshToken,
            .expiry = asp::SystemTime::fromUnix(refreshTokenExpiry),
        };
    }

    Token getAccessToken() const {
        return Token{
            .token = accessToken,
            .expiry = asp::SystemTime::fromUnix(accessTokenExpiry),
        };
    }
};

struct StorageData {
    struct ServerData {
        std::unordered_map<int32_t, AccountEntry> accounts;
    };

    geode::utils::StringMap<ServerData> servers;
};

class ArgonStorage : public SingletonBase<ArgonStorage> {
    friend class SingletonBase;
    ArgonStorage();

public:
    std::optional<Token> getAccessToken(std::string_view serverUrl, const AccountData& account);
    std::optional<Token> getRefreshToken(std::string_view serverUrl, const AccountData& account);

    void storeRefreshToken(std::string_view serverUrl, const AccountData& account, std::string refreshToken, uint64_t expiry);
    void storeAccessToken(std::string_view serverUrl, const AccountData& account, std::string accessToken, uint64_t expiry);

    void clearAllTokens();
    void clearTokens(int accountId);
    bool hasToken(const AccountData& account, std::string_view serverUrl);

private:
    asp::Mutex<StorageData, true> m_data;

    geode::Result<> load();
    geode::Result<> save();

    StorageData::ServerData& serverData(decltype(m_data)::Guard& guard, std::string_view serverUrl);
};

}
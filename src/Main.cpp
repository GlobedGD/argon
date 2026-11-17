#include <argon.hpp>

#include "Challenge.hpp"
#include "Overrides.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace asp::time;
using namespace arc;

namespace argon {

std::string_view authProgressToString(AuthProgress progress) {
    switch (progress) {
        case AuthProgress::RequestedChallenge:
            return "Requested challenge";
        case AuthProgress::SolvingChallenge:
            return "Solving challenge";
        case AuthProgress::VerifyingChallenge:
            return "Verifying solution";
        default:
            return "Unknown";
    }
}

AccountData getGameAccountData() {
    int accountId = GJAccountManager::get()->m_accountID;
    int userId = GameManager::get()->m_playerUserID;
    std::string username = GJAccountManager::get()->m_username;
    std::string gjp = GJAccountManager::get()->m_GJP2;

    return {
        .accountId = accountId,
        .userId = userId,
        .username = std::move(username),
        .gjp2 = std::move(gjp),
        .serverUrl = argon::web::getBaseServerUrl(),
    };
}

bool signedIn() {
    return GJAccountManager::get()->m_accountID > 0;
}

void clearAllTokens() {
    ArgonStorage::get().clearAllTokens();
}

void clearToken(const _overrides& overrides) {
    ArgonStorage::get().clearTokens(getAccountId(overrides));
}

bool hasToken(const _overrides& overrides) {
    auto account = getAccount(overrides);
    return ArgonStorage::get().hasToken(account, getArgonServerUrl(overrides));
}

static Future<Result<std::string>> tryRefresh(web::Client& client) {
    auto response = ARC_CO_UNWRAP(co_await client.sendRefresh());
    storeTokens(client, response);
    co_return Ok(response.access_token);
}

AuthFuture startAuth(AuthOptions options) {
    auto client = ARC_CO_UNWRAP(co_await makeClient(options._overrides));
    auto& serverUrl = client.m_serverUrl;
    auto& account = client.m_account;

    // use cached token if possible
    if (auto token = ArgonStorage::get().getAccessToken(serverUrl, account)) {
        log::debug("Using cached access token for account {}", account.username);
        co_return Ok(std::move(token->token));
    }

    // no access token, try refreshing if a refresh token exists
    auto result = co_await withRefreshToken(client, options._overrides, [&] -> Future<web::ArgonApiResult<std::string>> {
        // withRefreshToken may invoke /v2/login if our current refresh token is invalid, which basically does the whole job of refreshing for us,
        // so don't invoke refresh again if that did happen
        auto accessToken = ArgonStorage::get().getAccessToken(serverUrl, account);
        if (accessToken) {
            co_return Ok(accessToken->token);
        }

        auto resp = ARC_CO_UNWRAP(co_await client.sendRefresh());
        storeTokens(client, resp);
        co_return Ok(resp.access_token);
    });

    co_return result;
}

Future<Result<std::string>> openAccountPage(const _overrides& overrides) {
    auto client = ARC_CO_UNWRAP(co_await makeClient(overrides));
    auto result = co_await withRefreshToken(client, overrides, [&] {
        return client.sendGenerateWebToken();
    });

    if (!result) {
        co_return Err("failed to generate webtoken: {}", result.unwrapErr());
    }

    auto url = fmt::format("{}/login?token={}", client.m_serverUrl, result.unwrap().token);
    utils::web::openLinkInBrowser(url);

    co_return Ok(std::move(url));
}

}

$on_mod(Loaded) {
    ButtonSettingPressedEventV3(Mod::get(), "action-buttons").listen([] (auto buttonKey) {
        if (buttonKey == "website-login") {
            async::spawn(argon::openAccountPage(), [](auto result) {
                if (!result) {
                    queueInMainThread([e = std::move(result).unwrapErr()] {
                        FLAlertLayer::create(
                            "Argon Error",
                            fmt::format("Failed to open account page: {}", e),
                            "Ok"
                        )->show();
                    });
                }
            });
        }
    }).leak();
}

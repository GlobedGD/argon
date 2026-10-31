#include <argon.hpp>

#include "ArgonStorage.hpp"
#include "Web.hpp"

#include <arc/time/Sleep.hpp>
#include <asp/time/Duration.hpp>
#include <Geode/Geode.hpp>
#include <Geode/utils/terminate.hpp>
#include <thread>

using namespace geode::prelude;
using namespace asp::time;
using namespace arc;

namespace argon {

static std::optional<std::thread::id> g_mainThreadId;
// TODO: change on release
static const std::string_view g_officialServer = "https://argon-dev.globed.dev";

static std::string getArgonServerUrl(const _overrides& overrides) {
    if (overrides.serverUrl) {
        return *overrides.serverUrl;
    }

    // use saved value as secondary override
    auto customUrl = Mod::get()->getSavedValue<std::string>("custom-server-url");
    if (!customUrl.empty()) {
        return customUrl;
    }

    return std::string{g_officialServer};
}

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

void clearToken() {
    clearToken(GJAccountManager::get()->m_accountID);
}

void clearToken(int accountId) {
    ArgonStorage::get().clearTokens(accountId);
}

void clearToken(const AccountData& account) {
    clearToken(account.accountId);
}

bool hasToken(std::optional<std::string_view> argonServerUrl) {
    return hasToken(getGameAccountData(), argonServerUrl);
}

bool hasToken(const AccountData& account, std::optional<std::string_view> argonServerUrl) {
    return ArgonStorage::get().hasToken(account, argonServerUrl.value_or(g_officialServer));
}

AuthFuture startAuth(std::optional<AccountData> data) {
    return startAuth(AuthOptions{ .account = std::move(data) });
}

inline uint32_t solveChallenge(uint32_t value) {
    uint64_t s = value;
    s *= 0x9e3779b97f4a7c15;
    s ^= s >> 27;
    s *= 0x9e3779b1;
    s ^= s >> 31;
    return static_cast<uint32_t>(s);
}

static Future<Result<>> submitSolution(web::Client& client, web::ChallengeResponse& response, uint32_t solution) {
    if (response.type == web::ChallengeType::ProfileField) {
        ARC_CO_UNWRAP_INTO(auto data, co_await client.getGDCurrentProfileData());
        data.custom = fmt::format("argon-{}-{}", response.challenge_id, solution);

        co_return co_await client.sendGDUpdateProfile(std::move(data));
    } else if (response.type == web::ChallengeType::Comment) {
        auto content = fmt::format("argon-{}-{}", response.challenge_id, solution);
        co_return co_await client.sendGDSubmitComment(response.resource_id, std::move(content));
    } else {
        co_return Err("Unknown challenge method");
    }
}

static Future<std::string> troubleshootFailureCause(web::Client& client, web::ChallengeType type) {
    if (type == web::ChallengeType::ProfileField) {
        // only 1 possible failure
        co_return "Invalid account credentials, please try to Refresh Login in account settings";
    }
    // TODO: comment

    co_return "unknown error, all sanity checks succeeded";
}

struct SuccessfulAttempt {
    web::ChallengeResponse challenge;
    web::VerifyResponse verif;
};

struct AttemptResult {
    Result<SuccessfulAttempt> result;
    std::optional<web::ChallengeType> usedType;
};

static Future<AttemptResult> performAttempt(
    web::Client& client,
    const std::vector<web::ChallengeType>& supported,
    AuthOptions& options
) {
    auto progress = [&](AuthProgress p) {
        if (options.progress) options.progress(p);
    };

    progress(AuthProgress::RequestedChallenge);
    auto loginResult = co_await client.sendLogin(supported);
    if (!loginResult) {
        co_return AttemptResult {
            .result = Err(fmt::format("login request failed: {}", loginResult.unwrapErr().toString())),
            .usedType = std::nullopt,
        };
    }

    auto s1data = std::move(loginResult).unwrap();
    auto usedType = s1data.type;
    log::info("Solving challenge '{}'", usedType);

    progress(AuthProgress::SolvingChallenge);
    auto solution = solveChallenge(s1data.secret);

    auto submitResult = co_await submitSolution(client, s1data, solution);
    if (!submitResult) {
        co_return AttemptResult {
            .result = Err(fmt::format("failed to submit solution: {}", submitResult.unwrapErr())),
            .usedType = usedType,
        };
    }

    progress(AuthProgress::VerifyingChallenge);
    while (true) {
        auto s2res = co_await client.sendVerify(s1data.challenge_id, solution);
        if (!s2res) {
            auto msg = co_await troubleshootFailureCause(client, usedType);
            co_return AttemptResult {
                .result = Err(fmt::format("verify failed: {} ({})", s2res.unwrapErr().toString(), msg)),
                .usedType = usedType,
            };
        }

        auto data = std::move(s2res).unwrap();
        if (!data.completed) {
            // wait a bit and try again
            co_await arc::sleep(asp::Duration::fromSecs(1));
            continue;
        }

        // done!
        co_return AttemptResult {
            .result = Ok(SuccessfulAttempt {
                .challenge = std::move(s1data),
                .verif = std::move(data),
            }),
            .usedType = usedType,
        };
    }
}

static void storeTokens(web::Client& client, auto& response) {
    auto& storage = ArgonStorage::get();
    storage.storeRefreshToken(client.m_serverUrl, client.m_account, response.refresh_token, response.refresh_token_valid_until);
    storage.storeAccessToken(client.m_serverUrl, client.m_account, response.access_token, response.access_token_valid_until);
}

static web::RefreshResponse handleSuccessfulAuth(web::Client& client, SuccessfulAttempt& att) {
    web::RefreshResponse r{
        .refresh_token = att.verif.refresh_token.value(),
        .refresh_token_valid_until = att.verif.refresh_token_valid_until.value(),
        .access_token = att.verif.access_token.value(),
        .access_token_valid_until = att.verif.access_token_valid_until.value(),
    };
    storeTokens(client, r);

    // TODO: delete the comment?

    return r;
}

static Future<Result<std::string>> tryRefresh(web::Client& client) {
    auto response = ARC_CO_UNWRAP(co_await client.sendRefresh());
    storeTokens(client, response);
    co_return Ok(response.access_token);
}

static Future<Result<web::RefreshResponse>> performChallenge(web::Client& client, AuthOptions& options) {
    auto& account = client.m_account;
    log::info(
        "Starting authentication for account {} ({}), server: '{}'",
        account.username, account.accountId, account.serverUrl
    );

    std::vector<web::ChallengeType> supportedChallenges {
        web::ChallengeType::ProfileField,
        web::ChallengeType::Comment,
    };
    std::vector<std::string> failures;

    while (!supportedChallenges.empty()) {
        auto result = co_await performAttempt(client, supportedChallenges, options);
        if (!result.result) {
            auto err = result.result.unwrapErr();

            // if usedType is null, this means the server has general problems with authentication, not specific to this type
            if (!result.usedType) {
                log::warn("login failed: {}", result.usedType, err);
                failures.push_back(fmt::format("general: {}", err));
                break;
            }

            log::warn("{} attempt failed: {}", result.usedType, err);
            failures.push_back(fmt::format("{}: {}", result.usedType, err));

            std::erase_if(supportedChallenges, [&](const auto& t) {
                return t == result.usedType;
            });
            continue;
        }

        auto& att = result.result.unwrap();
        auto response = handleSuccessfulAuth(client, att);

        co_return Ok(std::move(response));
    }

    // if we reached here, this means all methods have failed!
    StringBuffer<> out;
    out.append("All authentication methods failed:\n");
    for (auto& f : failures) {
        out.append(" - {}\n", f);
    }

    co_return Err(out.str());
}

AuthFuture startAuth(AuthOptions options) {
    if (!options.account) {
        // no account, fetch it
        options.account = *co_await async::waitForMainThread(getGameAccountData);
    }

    if (!options.account->valid()) {
        co_return Err("Invalid account data");
    }

    auto& account = *options.account;
    auto serverUrl = getArgonServerUrl(options._overrides);

    // use cached token if possible
    if (auto token = ArgonStorage::get().getAccessToken(serverUrl, account)) {
        log::debug("Using cached access token for account {}", account.username);
        co_return Ok(std::move(token->token));
    }

    web::Client client {account, serverUrl, options._overrides.requestedMod, options._overrides.certVerification};

    // no access token, try refreshing if a refresh token exists
    if (auto token = ArgonStorage::get().getRefreshToken(serverUrl, account)) {
        log::debug("Using cached refresh token for account {}", account.username);
        client.m_refreshToken = std::move(token->token);

        auto refreshResult = co_await tryRefresh(client);
        if (refreshResult) {
            co_return Ok(std::move(refreshResult).unwrap());
        } else {
            log::warn("(Argon) failed to refresh access token: {}", refreshResult.unwrapErr());
            ArgonStorage::get().clearTokens(account.accountId);
        }
    }

    // perform the challenge
    auto result = ARC_CO_UNWRAP(co_await performChallenge(client, options));
    co_return Ok(std::move(result.access_token));
}

Future<Result<std::string>> openAccountPage(std::optional<AccountData> data, const _overrides& overrides) {
    if (!data) data = *co_await async::waitForMainThread(getGameAccountData);
    if (!data->valid()) {
        co_return Err("Invalid account data");
    }

    auto serverUrl = getArgonServerUrl(overrides);
    auto& account = *data;

    web::Client client {account, serverUrl, overrides.requestedMod, overrides.certVerification};

    if (auto token = ArgonStorage::get().getRefreshToken(serverUrl, account)) {
        client.m_refreshToken = token->token;
    } else {
        AuthOptions opts{};
        auto result = ARC_CO_UNWRAP(co_await performChallenge(client, opts));
        client.m_refreshToken = std::move(result.refresh_token);
    }

    auto result = ARC_CO_UNWRAP(co_await client.sendGenerateWebToken());

    auto url = fmt::format("{}/login?token={}", serverUrl, result.token);
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

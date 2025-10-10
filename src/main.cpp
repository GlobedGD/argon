#include <argon/argon.hpp>

#include "state.hpp"
#include "storage.hpp"
#include "web.hpp"

#include <asp/time/Duration.hpp>
#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace asp::time;

namespace argon {

Result<web::WebTask> startAuthInternal(const AccountData& account, std::string_view preferredMethod, bool forceStrong);

std::string authProgressToString(AuthProgress progress) {
    switch (progress) {
        case AuthProgress::RequestedChallenge:
            return "Requested challenge";
        case AuthProgress::SolvingChallenge:
            return "Solving challenge";
        case AuthProgress::VerifyingChallenge:
            return "Verifying solution";
        case AuthProgress::RetryingRequest:
            return "Requested challenge (retry)";
        case AuthProgress::RetryingSolve:
            return "Solving challenge (retry)";
        case AuthProgress::RetryingVerify:
            return "Verifying solution (retry)";
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

Result<> startAuth(AuthCallback callback, AuthProgressCallback progress, bool forceStrong) {
    auto data = getGameAccountData();
    if (data.accountId <= 0 || data.userId <= 0) {
        return Err("Not logged into a Geometry Dash account");
    }

    return startAuthWithAccount(std::move(data), std::move(callback), std::move(progress), forceStrong);
}

AuthLoginTask startAuthWithAccount(AccountData account, bool forceStrong) {
    auto [task, post, prog, cancel] = AuthLoginTask::spawn();

    if (auto err = startAuthWithAccount(account, post, prog, forceStrong).err())
        post(Err(*err));

    return task;
}

AuthLoginTask startAuth(bool forceStrong) {
    auto [task, post, prog, cancel] = AuthLoginTask::spawn();

    auto data = getGameAccountData();
    if (data.accountId <= 0 || data.userId <= 0) {
        Result<std::string> err = Err("Not logged into a Geometry Dash account");
        post(Err("Not logged into a Geometry Dash account"));
    }

    if (auto err = startAuthWithAccount(data, post, prog, forceStrong).err())
        post(Err(*err));

    return task;
}

Result<> startAuthWithAccount(AccountData account, AuthCallback callback, AuthProgressCallback progress, bool forceStrong) {
    auto& argon = ArgonState::get();
    auto _ = argon.lockServerUrl();

    // use cached token if possible
    if (auto token = ArgonStorage::get().getAuthToken(account, argon.getServerUrl())) {
        callback(std::move(Ok(token.value())));
        return Ok();
    }

    // disallow multiple concurring auth requests for the same acc
    if (auto dur = argon.isInProgress(account.accountId)) {
        if (dur > Duration::fromMinutes(2)) {
            argon.killAuthAttempt(account.accountId);
        } else {
            return Err("there is already an auth attempt in progress for this account");
        }
    }

    GEODE_UNWRAP_INTO(auto task, startAuthInternal(account, "message", forceStrong));

    argon.pushNewRequest(
        std::move(callback),
        std::move(progress),
        std::move(account),
        std::move(task),
        forceStrong
    );

    return Ok();
}

Result<web::WebTask> startAuthInternal(const AccountData& account, std::string_view preferredMethod, bool forceStrong) {
    if (account.accountId <= 0 || account.userId <= 0 || account.username.empty()) {
        return Err("Invalid account data");
    }

    initConfigLock();

    // Start stage 1 authentication.

    return Ok(argon::web::startStage1(account, preferredMethod, forceStrong));
}

Result<> setServerUrl(std::string url) {
    if (url.empty()) {
        return Err("Invalid server URL");
    }

    return ArgonState::get().setServerUrl(std::move(url));
}

void setCertVerification(bool state) {
    ArgonState::get().setCertVerification(state);
}

bool getCertVerification() {
    return ArgonState::get().getCertVerification();
}

void initConfigLock() {
    ArgonState::get().initConfigLock();
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

}

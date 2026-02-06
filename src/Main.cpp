#include <argon/argon.hpp>

#include "ArgonState.hpp"
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

static bool isMainThread() {
    if (!g_mainThreadId) {
        // try to be safe and not always return false
        return geode::utils::thread::getName() == "Main";
    }

    return std::this_thread::get_id() == *g_mainThreadId;
}

static void requireMainThread(std::string message) {
    if (isMainThread()) return;

    log::error("Argon - thread safety violation detected");
    if (!g_mainThreadId) {
        log::error("Load event was never ran - we don't know which thread is main.");
        log::error("Did you attempt to start authentication before the mod was fully loaded?");
    }

    geode::utils::terminate(
        fmt::format("{}\n\nPlease refer to the Argon README for information on how to use Argon in a thread-safe manner.", message)
    );
}

std::string_view authProgressToString(AuthProgress progress) {
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
    requireMainThread("`argon::getGameAccountData` called not in main thread - this is a bug in your mod, GD account data should only be accessed from the main thread.");

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

Result<> setServerUrl(std::string url) {
    if (url.empty()) {
        return Err("Invalid server URL");
    }

    ArgonState::get().setServerUrl(std::move(url));
    return Ok();
}

std::string getServerUrl() {
    return ArgonState::get().getServerUrl();
}

void setCertVerification(bool state) {
    ArgonState::get().setCertVerification(state);
}

bool getCertVerification() {
    return ArgonState::get().getCertVerification();
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

bool hasToken() {
    return hasToken(getGameAccountData());
}

bool hasToken(const AccountData& account) {
    return ArgonStorage::get().hasAuthToken(account, getServerUrl());
}


AuthFuture startAuth(AccountData data) {
    return startAuth(AuthOptions{ .account = std::move(data) });
}

inline std::string solveChallenge(int value) {
    return fmt::to_string(value ^ 0x5F3759DF);
}

static Future<Result<>> submitSolution(const AccountData& account, std::string_view solution, int id) {
    auto text = fmt::format("#ARGON# {}", solution);

    return web::submitGDMessage(account, id, text);
}

static Future<std::string> troubleshootFailureCause(const AccountData& account) {
    auto result = co_await web::checkGDMessageLimit(account);
    if (result.isErr()) {
        co_return std::move(result).unwrapErr();
    }
    co_return "Stage 2 failed due to unknown error, auth and message limit are OK";
}

AuthFuture startAuth(AuthOptions options) {
    if (!options.account.valid()) {
        co_return Err("Invalid account data");
    }

    auto& argon = ArgonState::get();

    // use cached token if possible
    if (auto token = ArgonStorage::get().getAuthToken(options.account, argon.getServerUrl())) {
        log::debug("(Argon) Using cached auth token for account {}", options.account.username);
        co_return Ok(std::move(*token));
    }

    log::debug(
        "(Argon) Starting authentication for account {} ({}), server: '{}'",
        options.account.username, options.account.accountId, options.account.serverUrl
    );

    auto progress = [&](AuthProgress p) {
        if (options.progress) options.progress(p);
    };

    progress(AuthProgress::RequestedChallenge);
    ARC_CO_UNWRAP_INTO(auto s1data, co_await web::startChallenge(options.account, "message", options.forceStrong));

    // TODO: in future try falling back to comment auth

    progress(AuthProgress::SolvingChallenge);
    auto solution = solveChallenge(s1data.challenge);
    auto s2res = co_await submitSolution(options.account, solution, s1data.id);
    if (!s2res) {
        co_return Err(co_await troubleshootFailureCause(options.account));
    }

    progress(AuthProgress::VerifyingChallenge);
    ARC_CO_UNWRAP_INTO(auto vdata, co_await web::verifyChallenge(options.account, s1data.challengeId, solution));

    auto startedAt = asp::Instant::now();
    auto latestDeadline = startedAt + asp::Duration::fromSecs(30);

    while (std::holds_alternative<web::PollLater>(vdata)) {
        auto& plater = std::get<web::PollLater>(vdata);
        auto waitTime = asp::Duration::fromMillis(plater.ms);
        auto now = asp::Instant::now();

        // take no longer than 30 seconds
        auto deadline = std::min(
            now + waitTime,
            latestDeadline
        );

        log::debug("(Argon) Waiting for {} and polling again..", waitTime.toString());
        co_await arc::sleepUntil(deadline);

        now = asp::Instant::now();
        if (now >= latestDeadline) {
            co_return Err("Server did not verify the solution in a reasonable amount of time");
        }

        // poll again
        ARC_CO_UNWRAP_INTO(vdata, co_await web::verifyChallengePoll(options.account, s1data.challengeId, solution));
    }

    auto& verif = std::get<web::SuccessfulVerification>(vdata);
    argon.handleSuccessfulAuth(options.account, verif.authtoken, s1data.ident, verif.commentId);

    co_return Ok(std::move(verif.authtoken));
}

$execute {
    ModStateEvent(ModEventType::Loaded, Mod::get()).listen([] {
        g_mainThreadId = std::this_thread::get_id();
        ArgonState::get().initConfigLock();
    }, -10000).leak();
}

}

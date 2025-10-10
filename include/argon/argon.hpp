#pragma once

#include <Geode/Result.hpp>
#include <Geode/utils/web.hpp>
#include <string>
#include <functional>

namespace argon {
    struct AccountData {
        int accountId;
        int userId;
        std::string username;
        std::string gjp2;
        std::string serverUrl;
    };

    enum class AuthProgress {
        RequestedChallenge,
        SolvingChallenge,
        VerifyingChallenge,

        RetryingRequest,
        RetryingSolve,
        RetryingVerify,
    };

    // Converts the `AuthProgress` enum to a human readable string,
    // e.g. "Requesting challenge", "Solving challenge"
    std::string authProgressToString(AuthProgress progress);

    using AuthCallback = std::function<void(geode::Result<std::string>)>;
    using TroubleshootCallback = std::function<void(geode::Result<>)>;
    using AuthProgressCallback = std::function<void(AuthProgress)>;
    using AuthLoginTask = geode::Task<geode::Result<std::string>, AuthProgress>;

    // Collects the account data of the currently logged in user. Not thread-safe.
    AccountData getGameAccountData();

    // Set the URL of the used Argon server, not thread-safe.
    geode::Result<> setServerUrl(std::string url);

    // Enable or disable SSL certificate verification, by default is enabled.
    void setCertVerification(bool state);

    // Get whether certificate verification is enabled
    bool getCertVerification();

    // Initializes the config lock structure for interoperability between other mods using Argon.
    // Should be called from the main thread once the game has at least reached the loading screen
    // (what matters is that GameManager::init has been run.)
    void initConfigLock();

    /* Starting auth */

    // Start authentication with the current user account.
    // `callback` is called with the authtoken once auth is complete, or with an error if one happens. It should always be called.
    // `progress` is an optional progress callback that will be called whenever the auth process reaches the next stage.
    //
    // **NOTE**: this function is not thread-safe. This function does not block, but if you still want to start auth on another thread,
    // you should first collect user's credentials with `argon::getGameAccountData` on main thread, and then call `argon::startAuthWithAccount` on your thread.
    // Additionally, make sure to call `argon::initConfigLock` at least once **on the main thread** before starting the auth attempt.
    geode::Result<> startAuth(AuthCallback callback, AuthProgressCallback progress = {}, bool forceStrong = false);

    // Start authentication with given user credentials.
    // `callback` is called with the authtoken once auth is complete, or with an error if one happens. It should always be called.
    // `progress` is an optional progress callback that will be called whenever the auth process reaches the next stage.
    //
    // This function can be safely called from any thread, and it won't block the thread.
    // **NOTE**: If calling not from main thread, make sure to call `argon::initConfigLock` at least once in your mod,
    // on the main thread and before calling this function. `$on_mod(Loaded)` can be a good place if this is not an early-load mod.
    geode::Result<> startAuthWithAccount(AccountData account, AuthCallback callback, AuthProgressCallback progress = {}, bool forceStrong = false);

    // Wrapper around startAuthWithAccount that returns an awaitable Task
    // This function can be safely called from any thread, and it won't block the thread.
    // **NOTE**: If calling not from main thread, make sure to call `argon::initConfigLock` at least once in your mod,
    // on the main thread and before calling this function. `$on_mod(Loaded)` can be a good place if this is not an early-load mod.
    AuthLoginTask startAuthWithAccount(AccountData account, bool forceStrong);

    // Wrapper around startAuth that returns an awaitable Task
    // **NOTE**: this function is not thread-safe. This function does not block, but if you still want to start auth on another thread,
    // you should first collect user's credentials with `argon::getGameAccountData` on main thread, and then call `argon::startAuthWithAccount` on your thread.
    // Additionally, make sure to call `argon::initConfigLock` at least once **on the main thread** before starting the auth attempt.
    AuthLoginTask startAuth(bool forceStrong);

    /* Managing tokens */

    // Clears all authtokens from the storage that use the same server URL as the current selected.
    // Don't use this unless there's a good reason to. Thread-safe.
    void clearAllTokens();

    // Clears all authtokens from the storage for the currently used GD account.
    // Only tokens generated with the same server URL are deleted. Not thread-safe, for thread safety use `(int)` overload.
    void clearToken();

    // Clears all authtokens from the storage for this account.
    // Only tokens generated with the same server URL are deleted. Thread-safe.
    void clearToken(int accountId);

    // Clears all authtokens from the storage for this account.
    // Only tokens generated with the same server URL are deleted. Thread-safe.
    void clearToken(const AccountData& account);

    /* Troubleshooting */

    geode::Result<> startTroubleshooter(TroubleshootCallback callback);
}

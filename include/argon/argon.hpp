#pragma once

#include <Geode/Result.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/function.hpp>
#include <string>

namespace argon {
    struct AccountData {
        int accountId = 0;
        int userId = 0;
        std::string username;
        std::string gjp2;
        std::string serverUrl;

        bool valid() const {
            return accountId > 0 && userId > 0 && !username.empty();
        }
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
    std::string_view authProgressToString(AuthProgress progress);

    // Collects the account data of the currently logged in user. Call only on main thread.
    AccountData getGameAccountData();

    // Returns whether the user is signed into a Geometry Dash account. Call only on main thread.
    bool signedIn();

    // Set the URL of the used Argon server, thread-safe.
    geode::Result<> setServerUrl(std::string url);

    // Get the URL of the used Argon server, thread-safe.
    std::string getServerUrl();

    // Enable or disable SSL certificate verification, by default is enabled.
    void setCertVerification(bool state);

    // Get whether certificate verification is enabled
    bool getCertVerification();

    /* Starting auth */

    using AuthProgressCallback = geode::Function<void(AuthProgress)>;
    using AuthFuture = arc::Future<geode::Result<std::string>>;

    struct AuthOptions  {
        AuthProgressCallback progress;
        std::optional<AccountData> account;
        bool forceStrong = false;
    };

    // Returns a future that will start authentication and return the authtoken once completed.
    // Shorthand for `startAuth({ .account = data })`
    // If account data is not provided, it will be fetched later, which will add a 1-2 frame delay due to waiting for main thread.
    AuthFuture startAuth(std::optional<AccountData> data = std::nullopt);

    // Returns a future that will start authentication and return the authtoken once completed.
    AuthFuture startAuth(AuthOptions options);

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

    // Checks if there's an authtoken stored for the currently used GD account.
    // Not thread-safe, for thread safety use `(const AccountData&)` overload.
    bool hasToken();

    // Checks if there's an authtoken stored for this account, thread-safe.
    // If this returns true, all auth functions will likely immediately return success.
    bool hasToken(const AccountData& account);
}

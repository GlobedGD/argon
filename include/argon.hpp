#pragma once

#include <Geode/Result.hpp>
#include <Geode/utils/function.hpp>
#include "config.hpp"
#include <string>

namespace argon {

struct AccountData {
    int accountId = 0;
    int userId = 0;
    std::string username;
    std::string gjp2;
    /// GD server URL, not argon
    std::string serverUrl;

    bool valid() const {
        return accountId > 0 && userId > 0 && !username.empty();
    }
};

enum class AuthProgress {
    RequestedChallenge,
    SolvingChallenge,
    VerifyingChallenge,
};

inline struct _overrides {
    std::optional<std::string> serverUrl;
    geode::Mod* requestedMod = geode::Mod::get();
    /// Set to false to disable TLS certificate verification, only do this if you know what you are doing.
    bool certVerification = true;
} g_overrides;

// Converts the `AuthProgress` enum to a human readable string,
// e.g. "Requesting challenge", "Solving challenge"
ARGON_DLL std::string_view authProgressToString(AuthProgress progress);

// Collects the account data of the currently logged in user. Call only on main thread.
ARGON_DLL AccountData getGameAccountData();

// Returns whether the user is signed into a Geometry Dash account. Call only on main thread.
ARGON_DLL bool signedIn();

/* Starting auth */

using AuthProgressCallback = geode::Function<void(AuthProgress)>;
using AuthFuture = arc::Future<geode::Result<std::string>>;

struct AuthOptions {
    AuthProgressCallback progress;
    std::optional<AccountData> account;
    const _overrides& _overrides = g_overrides;
};

// Returns a future that will start authentication and return the authtoken once completed.
// Shorthand for `startAuth({ .account = data })`
// If account data is not provided, it will be fetched later, which will add a 1-2 frame delay due to waiting for main thread.
ARGON_DLL AuthFuture startAuth(std::optional<AccountData> data = std::nullopt);

// Returns a future that will start authentication and return the authtoken once completed.
ARGON_DLL AuthFuture startAuth(AuthOptions options);

/* Managing tokens */

// Clears all authtokens from the storage that use the same server URL as the current selected.
// Don't use this unless there's a good reason to. Thread-safe.
ARGON_DLL void clearAllTokens();

// Clears all authtokens from the storage for the currently used GD account.
// Only tokens generated with the same server URL are deleted. Not thread-safe, for thread safety use `(int)` overload.
ARGON_DLL void clearToken();

// Clears all authtokens from the storage for this account.
// Only tokens generated with the same server URL are deleted. Thread-safe.
ARGON_DLL void clearToken(int accountId);

// Clears all authtokens from the storage for this account.
// Only tokens generated with the same server URL are deleted. Thread-safe.
ARGON_DLL void clearToken(const AccountData& account);

// Checks if there's an authtoken stored for the currently used GD account.
// Not thread-safe, for thread safety use `(const AccountData&)` overload.
// If `argonServerUrl` is null, assumes you're checking for the official Argon instance.
ARGON_DLL bool hasToken(std::optional<std::string_view> argonServerUrl = std::nullopt);

// Checks if there's an authtoken stored for this account, thread-safe.
// If this returns true, all auth functions will likely immediately return success.
// If `argonServerUrl` is null, assumes you're checking for the official Argon instance.
ARGON_DLL bool hasToken(const AccountData& account, std::optional<std::string_view> argonServerUrl = std::nullopt);

/// Overrides the server URL that will be used for Argon calls *from your mod*.
/// Note: this does not set any global setting, this sets a static variable that is local to your mod specifically.
/// All future calls to Argon functions, such as `startAuth` will use the provided argon server URL.
/// If `std::nullopt` is passed, the default Argon server is used, which will not work with any GDPS.
inline void overrideArgonServer(std::optional<std::string> serverUrl) {
    g_overrides.serverUrl = std::move(serverUrl);
}

/// Overrides whether certificate verification is enabled for calls to Argon and GD servers.
/// Note: this does not set any global setting, this sets a static variable that is local to your mod specifically.
inline void overrideCertVerification(bool verifyCertificates) {
    g_overrides.certVerification = verifyCertificates;
}

/// Opens the Argon account page in the user's browser. This page can be used to manage certain user settings.
/// If you'd rather not open it in the browser, pass `openLink = false`. The URL is always returned by this function.
///
/// This is a future, because it requires requesting an auth code from the Argon server first, before opening the page.
ARGON_DLL arc::Future<geode::Result<std::string>> openAccountPage(
    std::optional<AccountData> data = std::nullopt,
    const _overrides& overrides = g_overrides
);

}

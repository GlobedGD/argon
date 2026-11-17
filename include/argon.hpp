#pragma once

#include <Geode/utils/function.hpp>
#include "argon/base.hpp"
#include "argon/discord.hpp"
#include <string>

namespace argon {
enum class AuthProgress {
    RequestedChallenge,
    SolvingChallenge,
    VerifyingChallenge,
};

/// Converts the `AuthProgress` enum to a human readable string,
/// e.g. "Requesting challenge", "Solving challenge"
ARGON_DLL std::string_view authProgressToString(AuthProgress progress);

/// Collects the account data of the currently logged in user. Call only on main thread.
ARGON_DLL AccountData getGameAccountData();

/// Returns whether the user is signed into a Geometry Dash account. Call only on main thread.
ARGON_DLL bool signedIn();

/* Starting auth */

using AuthProgressCallback = geode::Function<void(AuthProgress)>;
using AuthFuture = Future<Result<std::string>>;

struct AuthOptions {
    AuthProgressCallback progress;
    const _overrides& _overrides = g_overrides;
};

/// Returns a future that will start authentication and return the authtoken once completed.
/// The returned token is an access token, meaning it is valid for a short period of time for the `/v2/token/validate` endpoint, and not much more.
ARGON_DLL AuthFuture startAuth(AuthOptions options = {});

/* Managing tokens */

/// Clears all authtokens from the storage that use the same server URL as the current selected.
/// Don't use this unless there's a good reason to. Thread-safe.
ARGON_DLL void clearAllTokens();

/// Clears all authtokens from the storage for the currently used GD account.
/// Only tokens generated with the same server URL are deleted.
/// This isn't thread-safe unless `overrideAccount` is called, since it needs to access GJAccountManager to get current account ID.
ARGON_DLL void clearToken(const _overrides& overrides = g_overrides);

/// Checks if there's an authtoken stored for the currently used GD account.
/// If this returns true, `startAuth` will likely immediately return success.
/// This isn't thread-safe unless `overrideAccount` is called, since it needs to access GJAccountManager to get current account ID.
ARGON_DLL bool hasToken(const _overrides& overrides = g_overrides);

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

/// Overrides the account used for all Argon calls *from your mod*.
/// By default, Argon will fetch the credentials from GJAccountManager, but this can be used to override it
inline void overrideAccount(std::optional<AccountData> account) {
    g_overrides.account = std::move(account);
}

/// Opens the Argon account page in the user's browser. This page can be used to manage certain user settings.
/// If you'd rather not open it in the browser, pass `openLink = false`. The URL is always returned by this function.
///
/// This is a future, because it requires requesting an auth code from the Argon server first, before opening the page.
ARGON_DLL Future<Result<std::string>> openAccountPage(const _overrides& overrides = g_overrides);

}

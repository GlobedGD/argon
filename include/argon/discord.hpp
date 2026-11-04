#pragma once
#include "base.hpp"

namespace argon::discord {

struct UserData {
    uint64_t id;
    std::string username;
    std::string avatar_url;
    asp::SystemTime last_sync_time;
};

/// Checks if the user exists in the Argon database and has a Discord account linked, and returns the details of that Discord account.
ARGON_DLL Future<Result<std::optional<UserData>>> getLinkState(const _overrides& overrides = g_overrides);

/// Starts the process of linking a Discord account, returning the OAuth2 URL.
/// You should open the URL in a browser so the user confirms the authorization, which will complete the linking process.
///
/// This generally shouldn't be needed to use directly, as you can simply open the account management website via `openAccountPage`,
/// where the user can perform the process by themselves, but this can be used for greater control over the process.
ARGON_DLL Future<Result<std::string>> link(const _overrides& overrides = g_overrides);

/// Unlinks the Discord account from the Argon account, if one is linked.
ARGON_DLL Future<Result<>> unlink(const _overrides& overrides = g_overrides);

}
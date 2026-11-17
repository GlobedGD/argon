#pragma once
#include <Geode/Result.hpp>
#include "config.hpp"

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

inline struct _overrides {
    std::optional<AccountData> account;
    std::optional<std::string> serverUrl;
    geode::Mod* requestedMod = geode::Mod::get();
    /// Set to false to disable TLS certificate verification, only do this if you know what you are doing.
    bool certVerification = true;
} g_overrides;

using arc::Future;
using geode::Result;
using geode::Ok;
using geode::Err;

}

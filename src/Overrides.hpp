#pragma once
#include <Geode/utils/async.hpp>
#include <argon.hpp>
#include "Web.hpp"

using namespace geode::prelude;

namespace argon {

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

static AccountData getAccount(const _overrides& overrides) {
    if (overrides.account) {
        return *overrides.account;
    }

    return getGameAccountData();
}

static Future<AccountData> getAccountAsync(const _overrides& overrides) {
    if (overrides.account) {
        co_return *overrides.account;
    }

    co_return *co_await async::waitForMainThread(getGameAccountData);
}

static int32_t getAccountId(const _overrides& overrides) {
    if (overrides.account) {
        return overrides.account->accountId;
    }

    return GJAccountManager::get()->m_accountID;
}

static Future<int32_t> getAccountIdAsync(const _overrides& overrides) {
    if (overrides.account) {
        co_return overrides.account->accountId;
    }

    co_return *co_await async::waitForMainThread([] {
        return GJAccountManager::get()->m_accountID;
    });
}

static Future<Result<AccountData>> getValidAccountAsync(const _overrides& overrides) {
    auto account = co_await getAccountAsync(overrides);
    if (!account.valid()) {
        if (overrides.account) {
            co_return Err("Invalid account data");
        } else {
            co_return Err("Not logged in to Geometry Dash, please log in and try again");
        }
    }

    co_return Ok(std::move(account));
}

static Future<Result<argon::web::Client>> makeClient(const _overrides& overrides) {
    auto account = ARC_CO_UNWRAP(co_await getValidAccountAsync(overrides));
    auto serverUrl = getArgonServerUrl(overrides);

    co_return Ok(argon::web::Client(std::move(account), std::move(serverUrl), overrides.requestedMod, overrides.certVerification));
}

}

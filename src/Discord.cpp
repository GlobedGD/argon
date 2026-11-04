#include <argon.hpp>

#include "Challenge.hpp"
#include "Overrides.hpp"

using namespace geode::prelude;
using namespace asp::time;
using namespace arc;

namespace argon::discord {

Future<Result<std::optional<UserData>>> getLinkState(const _overrides& overrides) {
    auto client = ARC_CO_UNWRAP(co_await makeClient(overrides));

    auto data = ARC_CO_UNWRAP(co_await withRefreshToken(client, overrides, [&] {
        return client.sendGetDiscordState();
    }));

    if (data.linked) {
        co_return Ok(data.discord_user);
    } else {
        co_return Ok(std::nullopt);
    }
}

Future<Result<std::string>> link(const _overrides& overrides) {
    auto client = ARC_CO_UNWRAP(co_await makeClient(overrides));

    auto data = ARC_CO_UNWRAP(co_await withRefreshToken(client, overrides, [&] {
        return client.sendDiscordLink();
    }));

    co_return Ok(data.authorize_url);
}

Future<Result<>> unlink(const _overrides& overrides) {
    auto client = ARC_CO_UNWRAP(co_await makeClient(overrides));
    ARC_CO_UNWRAP(co_await withRefreshToken(client, overrides, [&] {
        return client.sendDiscordUnlink();
    }));

    co_return Ok();
}

}

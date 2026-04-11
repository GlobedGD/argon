#pragma once
#include <argon/argon.hpp>
#include <Geode/utils/web.hpp>
#include "WebData.hpp"

namespace argon::web {

std::string getBaseServerUrl();

struct SuccessfulVerification {
    std::string authtoken;
    int commentId;
};

struct PollLater {
    uint32_t ms;
};

using VerifyResult = geode::Result<std::variant<SuccessfulVerification, PollLater>>;

arc::Future<geode::Result<Stage1ResponseData>> startChallenge(const AccountData& account, std::string_view preferredMethod, bool forceStrong);
arc::Future<VerifyResult> verifyChallenge(const AccountData& account, uint32_t challengeId, std::string_view solution);
arc::Future<VerifyResult> verifyChallengePoll(const AccountData& account, uint32_t challengeId, std::string_view solution);

arc::Future<geode::Result<>> submitGDMessage(const AccountData& account, int target, std::string_view message);
arc::Future<geode::Result<>> deleteGDMessage(const AccountData& account, int id);
arc::Future<geode::Result<>> submitGDComment(const AccountData& account, int target, std::string_view message);
arc::Future<geode::Result<>> checkGDMessageLimit(const AccountData& account);
arc::Future<geode::Result<>> checkGDUserNotBlocked(const AccountData& account, int targetUser);

}

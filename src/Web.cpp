#include "ArgonState.hpp"
#include "WebData.hpp"
#include "Web.hpp"
#ifdef GEODE_IS_ANDROID
#include <Geode/binding/GJMoreGamesLayer.hpp>
#endif
#include <Geode/loader/Mod.hpp>
#include <asp/iter.hpp>
#include "external/ServerAPIEvents.hpp"

using namespace arc;

namespace argon::web {
// TODO: reverse engineer the "encryption" and replace this all with geode base64

static std::string base64Encode(const gd::string& data) {
    // trust me i did not want to use this but oh well
    std::string ret = cocos2d::ZipUtils::base64URLEncode(data);
    ret.resize(strlen(ret.c_str()));

    return ret;
}

static std::string base64EncodeEnc(const gd::string& data, gd::string key) {
    std::string ret = ZipUtils::base64EncodeEnc(data, key);
    ret.resize(strlen(ret.c_str()));

    return ret;
}

template <int GDVer, size_t Off, size_t Alt>
struct Offset {
    static constexpr size_t value = Off;
    static constexpr size_t alt = Alt;
    static constexpr bool valid = (GEODE_COMP_GD_VERSION == GDVer);

    const char* addr(bool alt = false) const {
        return (const char*)(base::get() + (alt ? this->alt : this->value));
    }
};

// The addresses are pointing to "https://www.boomlings.com/database/getGJLevels21.php"
// in the main game executable
static constexpr
    GEODE_WINDOWS  (Offset<22081, 0x558b70, 0x0>)
    GEODE_ARM_MAC  (Offset<22081, 0x77d709, 0x0>)
    GEODE_INTEL_MAC(Offset<22081, 0x868df0, 0x0>)
    GEODE_ANDROID64(Offset<22081, 0xeccf90, 0x0>)
    GEODE_ANDROID32(Offset<22081, 0x96c0db, 0x0>)
    GEODE_IOS      (Offset<22081, 0x6b8cc2, 0x0>)
g_urlOffset;

static_assert(g_urlOffset.valid, "Unsupported GD version");

std::string getBaseServerUrl() {
    if (Loader::get()->isModLoaded("km7dev.server_api")) {
        auto url = ServerAPIEvents::getCurrentServer().url;
        if (!url.empty() && url != "NONE_REGISTERED") {
            while (url.ends_with('/')) {
                url.pop_back();
            }

            log::info("Using GDPS url: {}", url);
            return url;
        }
    }

    // This was taken from the impostor mod :) and altered

    bool isAmazonStore = false
        GEODE_ANDROID(|| !((GJMoreGamesLayer* volatile)nullptr)->getMoreGamesList()->count() );

    std::string ret = g_urlOffset.addr(isAmazonStore);

    // 34 chars: https://www.boomlings.com/database
    if(ret.size() > 34) ret = ret.substr(0, 34);

    while (ret.ends_with('/')) {
        ret.pop_back();
    }

    if (!ret.starts_with("http")) {
        log::error("(Argon) the base server URL does not appear to be valid: '{}'", ret);
        log::error("(Argon) the offset may be invalid, we used base + {:x} (alt: {})", isAmazonStore ? g_urlOffset.alt : g_urlOffset.value, isAmazonStore);
    }

    return ret;
}

static const char* platformString() {
#ifdef GEODE_IS_MACOS
# ifdef GEODE_IS_ARM_MAC
    return "MacOS arm64";
# else
    return "MacOS x86_64";
# endif
#elif defined GEODE_IS_WINDOWS
    return "Windows";
#elif defined GEODE_IS_ANDROID64
    return "Android64";
#elif defined GEODE_IS_ANDROID32
    return "Android32";
#elif defined GEODE_IS_IOS
    return "iOS";
#else
    return "Unknown";
#endif
}


static std::string getUserAgent() {
    return fmt::format("argon/v{} ({}, Geode {}, GD {})",
            ARGON_VERSION,
            platformString(),
            Loader::get()->getVersion(),
            Loader::get()->getGameVersion());
}

static std::string getReqMod() {
    auto mod = Mod::get();
    return fmt::format("{}/{}", mod->getID(), mod->getVersion().toVString());
}

static WebRequest baseRequest() {
    auto& argon = ArgonState::get();
    return WebRequest()
        .userAgent(getUserAgent())
        .certVerification(argon.getCertVerification())
        .timeout(std::chrono::seconds(10));
}

static WebRequest baseGDRequest() {
    auto& argon = ArgonState::get();
    return WebRequest()
        .userAgent("")
        .certVerification(argon.getCertVerification())
        .timeout(std::chrono::seconds(20));
}

struct Form {
    StringBuffer<> buf;

    Form& param(std::string_view key, std::string_view value) {
        if (buf.size() > 0) {
            buf.append('&');
        }

        buf.append(key);
        buf.append('=');
        buf.append(value);
        return *this;
    }

    template <std::integral T>
    Form& param(std::string_view key, T value) {
        return param(key, fmt::to_string(value));
    }

    std::string str() {
        return buf.str();
    }
};

Result<WebResponse> wrapResponse(std::string_view what, WebResponse response) {
    if (response.ok()) return Ok(std::move(response));
    return Err(wrapError(response, what));
}

Future<Result<Stage1ResponseData>> startChallenge(const AccountData& account, std::string_view preferredMethod, bool forceStrong) {
    auto& argon = ArgonState::get();

    auto payload = matjson::makeObject({
        {"accountId", account.accountId},
        {"userId", account.userId},
        {"username", account.username},
        {"forceStrong", forceStrong},
        {"reqMod", getReqMod()},
        {"preferred", preferredMethod}
    });

    auto url = argon.makeUrl("v1/challenge/start");
    log::debug("(Argon) requesting challenge with url: {}", url);

    auto response = co_await baseRequest()
        .bodyJSON(payload)
        .post(std::move(url));

    ARC_CO_UNWRAP_INTO(response, wrapResponse("challenge start", std::move(response)));
    co_return extractData<Stage1ResponseData>(response);
}

static Future<VerifyResult> verifyChallengeInner(const AccountData& account, uint32_t challengeId, std::string_view solution, std::string path) {
    auto& argon = ArgonState::get();

    auto payload = matjson::makeObject({
        {"challengeId", challengeId},
        {"accountId", account.accountId},
        {"solution", solution}
    });

    auto response = co_await baseRequest()
        .bodyJSON(payload)
        .post(argon.makeUrl(path));
    ARC_CO_UNWRAP_INTO(response, wrapResponse("challenge verify", std::move(response)));
    ARC_CO_UNWRAP_INTO(auto data, extractData<matjson::Value>(response));

    bool verified = data["verified"].asBool().unwrapOr(false);
    if (verified) {
        auto authtoken = data["authtoken"].asString().unwrapOrDefault();
        if (authtoken.empty()) {
            co_return Err("Malformed server response (missing auth token)");
        }

        int comment = data["commentId"].asInt().unwrapOrDefault();
        co_return Ok(SuccessfulVerification {
            .authtoken = std::move(authtoken),
            .commentId = comment
        });
    }

    auto pollAfter = data["pollAfter"].asInt().unwrapOr(1000);
    co_return Ok(PollLater(pollAfter));
}

Future<VerifyResult> verifyChallenge(const AccountData& account, uint32_t challengeId, std::string_view solution) {
    return verifyChallengeInner(account, challengeId, solution, "v1/challenge/verify");
}

Future<VerifyResult> verifyChallengePoll(const AccountData& account, uint32_t challengeId, std::string_view solution) {
    return verifyChallengeInner(account, challengeId, solution, "v1/challenge/verifypoll");
}

Future<Result<>> submitGDMessage(const AccountData& account, int target, std::string_view message) {
    auto response = co_await baseGDRequest()
        .bodyString(Form{}
            .param("accountID", account.accountId)
            .param("gjp2", account.gjp2)
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("secret", "Wmfd2893gb7")
            .param("toAccountID", target)
            .param("subject", base64Encode(gd::string{message.data(), message.size()}))
            .param("body", base64EncodeEnc("This is a message sent to verify your account, it can be safely deleted.", "14251"))
            .str())
        .post(fmt::format("{}/uploadGJMessage20.php", account.serverUrl));
    ARC_CO_UNWRAP_INTO(response, wrapResponse("GD message", std::move(response)));

    auto res = response.string().unwrapOrDefault();
    if (res.empty() || res == "-1") {
        co_return Err(wrapError(response, "GD message"));
    }

    co_return Ok();
}

Future<Result<>> submitGDUpdateProfile(const AccountData& account, UpdateProfilePayload payload) {
    auto response = co_await baseGDRequest()
        .bodyString(Form{}
            .param("accountID", account.accountId)
            .param("gjp2", account.gjp2)
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("secret", "Wmfv3899gc9")
            .param("mS", payload.messages)
            .param("frS", payload.friendRequests)
            .param("cS", payload.commentHistory)
            .param("yt", payload.youtube)
            .param("twitter", payload.twitter)
            .param("twitch", payload.twitch)
            .param("instagram", payload.instagram)
            .param("tiktok", payload.tiktok)
            .param("discord", payload.discord)
            .param("custom", payload.custom)
            .str())
        .post(fmt::format("{}/updateGJAccSettings20.php", account.serverUrl));
    ARC_CO_UNWRAP_INTO(response, wrapResponse("GD update account", std::move(response)));

    auto res = response.string().unwrapOrDefault();
    if (res.empty() || res == "-1") {
        co_return Err(wrapError(response, "GD update account"));
    }

    co_return Ok();
}

Future<Result<>> deleteGDMessage(const AccountData& account, int id) {
    // delete the message
    auto response = co_await baseGDRequest()
        .bodyString(Form{}
            .param("accountID", account.accountId)
            .param("gjp2", account.gjp2)
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("secret", "Wmfd2893gb7")
            .param("isSender", 1)
            .param("messageID", id)
            .str())
        .post(fmt::format("{}/deleteGJMessages20.php", account.serverUrl));

    ARC_CO_UNWRAP_INTO(response, wrapResponse("delete GD message", std::move(response)));

    co_return Ok();
}

Future<Result<>> submitGDComment(const AccountData& account, int target, std::string_view message) {
    co_return Err("Comment auth not yet implemented");
}

Future<Result<>> checkGDMessageLimit(const AccountData& account) {
    auto response = co_await baseGDRequest()
        .bodyString(Form{}
            .param("accountID", account.accountId)
            .param("gjp2", account.gjp2)
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("secret", "Wmfd2893gb7")
            .param("count", 50)
            .param("page", 7)
            .param("getSent", 1)
            .str())
        .post(fmt::format("{}/getGJMessages20.php", account.serverUrl));

    ARC_CO_UNWRAP_INTO(response, wrapResponse("fetch GD messages", std::move(response)));
    auto str = response.string().unwrapOrDefault();
    if (str.empty()) {
        co_return Err(wrapError(response, "fetch GD messages"));
    }

    if (str == "-1") {
        co_return Err("Invalid account credentials, please try to Refresh Login in account settings");
    }

    size_t msgCount = 0;
    if (str != "-2") {
        msgCount = asp::iter::split(str, '|').count();
    }

    if (msgCount == 50) {
        co_return Err("Sent message limit reached, please open your GD profile, go to the Sent Messages tab (not received), delete some messages and try again.");
    }

    co_return Ok();
}

Future<Result<>> checkGDUserNotBlocked(const AccountData& account, int targetUser) {
    auto response = co_await baseGDRequest()
        .bodyString(Form{}
            .param("accountID", account.accountId)
            .param("gjp2", account.gjp2)
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("secret", "Wmfd2893gb7")
            .param("type", 1)
            .param("dvs", 3)
            .str())
        .post(fmt::format("{}/getGJUserList20.php", account.serverUrl));

    ARC_CO_UNWRAP_INTO(response, wrapResponse("fetch GD blocklist", std::move(response)));
    auto str = response.string().unwrapOrDefault();
    if (str.empty()) {
        co_return Err(wrapError(response, "fetch GD messages"));
    }

    if (str == "-1") {
        co_return Err("Invalid account credentials, please try to Refresh Login in account settings");
    } else if (str == "-2") {
        co_return Ok();
    } else if (str.starts_with("-")) {
        co_return Err("Unexpected server response while fetching blocklist: {}", str);
    }

    std::string targetStr = fmt::to_string(targetUser);

    for (auto user : asp::iter::split(str, "|")) {
        for (auto [k, v] : asp::iter::split(user, ':').arrayChunks<2>()) {
            if (k == "16" && v == targetStr) {
                co_return Err("You have blocked the authentication bot account, please unblock it and try again");
            }
        }
    }

    co_return Ok();
}


Future<Result<web::UpdateProfilePayload>> getCurrentProfileData(const AccountData& account) {
    auto response = co_await baseGDRequest()
        .bodyString(Form{}
            .param("targetAccountID", account.accountId)
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("secret", "Wmfd2893gb7")
            .param("dvs", 3)
            .str())
        .post(fmt::format("{}/getGJUserInfo20.php", account.serverUrl));

    ARC_CO_UNWRAP_INTO(response, wrapResponse("fetch GD profile", std::move(response)));
    auto str = response.string().unwrapOrDefault();
    if (str.empty()) {
        co_return Err(wrapError(response, "fetch GD profile"));
    }

    if (str == "-1") {
        co_return Err("User not found");
    }

    web::UpdateProfilePayload out;

    for (auto [k, v] : asp::iter::split(str, ':').arrayChunks<2>()) {
        int key = utils::numFromString<int>(k).unwrapOr(-1);

        switch (key) {
            case 18: out.messages = utils::numFromString<int>(v).unwrapOr(0); break;
            case 19: out.friendRequests = utils::numFromString<int>(v).unwrapOr(0); break;
            case 50: out.commentHistory = utils::numFromString<int>(v).unwrapOr(0); break;
            case 20: out.youtube = v; break;
            case 44: out.twitter = v; break;
            case 45: out.twitch = v; break;
            case 58: out.discord = v; break;
            case 59: out.instagram = v; break;
            case 60: out.tiktok = v; break;
            case 61: out.custom = v; break;
        }
    }

    co_return Ok(std::move(out));
}

}
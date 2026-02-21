#include "ArgonState.hpp"
#include "WebData.hpp"
#include "Web.hpp"
#ifdef GEODE_IS_ANDROID
#include <Geode/binding/GJMoreGamesLayer.hpp>
#endif
#include <Geode/loader/Mod.hpp>
#include <asp/iter.hpp>

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
    // TODO: server api stuff
    // if (Loader::get()->isModLoaded("km7dev.server_api")) {
    //     auto url = ServerAPIEvents::getCurrentServer().url;
    //     if (!url.empty() && url != "NONE_REGISTERED") {
    //         while (url.ends_with('/')) {
    //             url.pop_back();
    //         }

    //         return url;
    //     }
    // }

    // This was taken from the impostor mod :) and altered

    bool isAmazonStore = false
        GEODE_ANDROID(|| !((GJMoreGamesLayer* volatile)nullptr)->getMoreGamesList()->count() );

    std::string ret = g_urlOffset.addr(isAmazonStore);

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
    auto payload = fmt::format(
        "accountID={}&gjp2={}&gameVersion=22&binaryVersion=45"
        "&secret=Wmfd2893gb7&toAccountID={}&subject={}&body={}",
        account.accountId, account.gjp2, target, base64Encode(gd::string{message.data(), message.size()}),
        base64EncodeEnc("This is a message sent to verify your account, it can be safely deleted.", "14251")
    );

    auto response = co_await baseGDRequest()
        .bodyString(payload)
        .post(fmt::format("{}/uploadGJMessage20.php", account.serverUrl));
    ARC_CO_UNWRAP_INTO(response, wrapResponse("GD message", std::move(response)));

    auto res = response.string().unwrapOrDefault();
    if (res.empty() || res == "-1") {
        co_return Err(wrapError(response, "GD message"));
    }

    co_return Ok();
}

Future<Result<>> deleteGDMessage(const AccountData& account, int id) {
    auto payload = fmt::format(
        "accountID={}&gjp2={}&gameVersion=22&binaryVersion=45"
        "&secret=Wmfd2893gb7&isSender=1&messageID={}",
        account.accountId, account.gjp2, id
    );

    // delete the message
    auto response = co_await baseGDRequest()
        .bodyString(payload)
        .post(fmt::format("{}/deleteGJMessages20.php", account.serverUrl));

    ARC_CO_UNWRAP_INTO(response, wrapResponse("delete GD message", std::move(response)));

    co_return Ok();
}

Future<Result<>> submitGDComment(const AccountData& account, int target, std::string_view message) {
    co_return Err("Comment auth not yet implemented");
}

Future<Result<>> checkGDMessageLimit(const AccountData& account) {
    auto payload = fmt::format(
        "accountID={}&gjp2={}&gameVersion=22&binaryVersion=45"
        "&secret=Wmfd2893gb7&count=50&page=7&getSent=1",
        account.accountId, account.gjp2
    );

    auto response = co_await baseGDRequest()
        .bodyString(payload)
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
        co_return Err("Sent message limit reached, please try deleting some sent messages");
    }

    co_return Ok();
}

}
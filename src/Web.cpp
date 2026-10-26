#include "Web.hpp"
#ifdef GEODE_IS_ANDROID
#include <Geode/binding/GJMoreGamesLayer.hpp>
#endif
#include <Geode/loader/Mod.hpp>
#include <asp/iter.hpp>
#include "external/sha1.hpp"

// TODO: wait for the lib to be ported
#if 0
# include "external/ServerAPIEvents.hpp"
#endif

using namespace arc;

namespace argon::web {
// TODO: reverse engineer the "encryption" and replace this all with geode base64

static std::string base64Encode(const std::string& data) {
    // trust me i did not want to use this but oh well
    std::string ret = cocos2d::ZipUtils::base64URLEncode(data);
    ret.resize(strlen(ret.c_str()));

    return ret;
}

static std::string base64EncodeEnc(const std::string& data, std::string key) {
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
    GEODE_WINDOWS  (Offset<22091, 0x757cd8, 0x0>)
    GEODE_ARM_MAC  (Offset<22081, 0x77d709, 0x0>)
    GEODE_INTEL_MAC(Offset<22081, 0x868df0, 0x0>)
    GEODE_ANDROID64(Offset<22081, 0xeccf90, 0x0>)
    GEODE_ANDROID32(Offset<22081, 0x96c0db, 0x0>)
    GEODE_IOS      (Offset<22081, 0x6b8cc2, 0x0>)
g_urlOffset;

static_assert(g_urlOffset.valid, "Unsupported GD version");

std::string getBaseServerUrl() {
    // if (Loader::get()->isModLoaded("km7dev.server_api")) {
    //     auto url = ServerAPIEvents::getCurrentServer().url;
    //     if (!url.empty() && url != "NONE_REGISTERED") {
    //         while (url.ends_with('/')) {
    //             url.pop_back();
    //         }

    //         log::info("Using GDPS url: {}", url);
    //         return url;
    //     }
    // }

    // TODO: amazon is irrelevant now?
    // bool isAmazonStore = false
    //     GEODE_ANDROID(|| !((GJMoreGamesLayer* volatile)nullptr)->getMoreGamesList()->count() );
    bool isAmazonStore = false;

    std::string ret = g_urlOffset.addr(isAmazonStore);

    // 34 chars: https://www.boomlings.com/database
    if (ret.size() > 34) ret = ret.substr(0, 34);

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


std::string Client::getUserAgent() {
    return fmt::format("argon/v{} ({}, Geode {}, GD {})",
            Mod::get()->getVersion(),
            platformString(),
            Loader::get()->getVersion(),
            Loader::get()->getGameVersion());
}

static std::string formatReqMod(Mod* mod) {
    if (!mod) return "unknown";
    return fmt::format("{}/{}", mod->getID(), mod->getVersion().toVString());
}

Result<WebResponse> wrapResponse(std::string_view what, WebResponse response) {
    if (response.ok()) return Ok(std::move(response));
    return Err(wrapError(response, what));
}

struct LoginPayload {
    int account_id;
    std::string requesting_mod;
    std::vector<ChallengeType> supported_challenges;
};

Future<ArgonApiResult<ChallengeResponse>> Client::sendLogin(std::span<const ChallengeType> supported) {
    LoginPayload payload {
        m_account.accountId,
        formatReqMod(m_reqMod),
        std::vector<ChallengeType>{supported.begin(), supported.end()}
    };

    co_return co_await this->sendArgonRequest<ChallengeResponse>(payload, "v2/account/login");
}

struct VerifyPayload {
    uint32_t challenge_id;
    int32_t account_id;
    uint32_t answer;
};

Future<ArgonApiResult<VerifyResponse>> Client::sendVerify(uint32_t challengeId, uint32_t answer) {
    VerifyPayload payload { challengeId, m_account.accountId, answer };

    co_return co_await this->sendArgonRequest<VerifyResponse>(payload, "v2/account/verify");
}

Future<ArgonApiResult<RefreshResponse>> Client::sendRefresh() {
    return this->sendArgonRequest<RefreshResponse>(std::monostate{}, "v2/account/refresh");
}

Future<utils::web::WebResponse> Client::sendGDRequest(
    std::optional<Form> payload,
    std::string_view suffix
) {
    auto req = WebRequest()
        .userAgent("")
        .certVerification(m_certVerification)
        .timeout(std::chrono::seconds(10));

    if (payload) {
        req.bodyString(payload->str());
        req.header("Content-Type", "application/x-www-form-urlencoded");
    }

    std::string url = fmt::format("{}/{}", m_account.serverUrl, suffix);
    co_return co_await req.post(std::move(url));
}

Future<Result<>> Client::sendGDUpdateProfile(UpdateProfilePayload payload) {
    auto response = co_await this->sendGDRequest(
        Form{}
            .param("accountID", m_account.accountId)
            .param("gjp2", m_account.gjp2)
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
            .param("custom", payload.custom),
        "updateGJAccSettings20.php"
    );

    ARC_CO_UNWRAP_INTO(response, wrapResponse("GD update account", std::move(response)));
    auto str = response.string().unwrapOrDefault();
    if (str.empty() || str == "-1") {
        co_return Err(wrapError(response, "GD update account"));
    }

    co_return Ok();
}

static std::string commentChk(std::string_view username, std::string_view comment, int levelId, int percent, int type) {
    auto input = fmt::format("{}{}{}{}{}{}", username, comment, levelId, percent, type, "xPT6iUrtws0J");
    SHA1 s{};
    s.update(input);
    auto hex = s.final();
    auto chk = base64EncodeEnc(hex, "29481");
    return chk;
}

Future<Result<>> Client::sendGDSubmitComment(int32_t levelId, std::string content) {
    auto comment = base64Encode(content);
    auto response = co_await this->sendGDRequest(
        Form{}
            .param("accountID", m_account.accountId)
            .param("gjp2", m_account.gjp2)
            .param("userName", m_account.username)
            .param("secret", "Wmfd2893gb7")
            .param("levelID", levelId)
            .param("chk", commentChk(m_account.username, comment, levelId, 0, 0))
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("comment", comment),
        "uploadGJComment21.php"
    );

    ARC_CO_UNWRAP_INTO(response, wrapResponse("GD submit comment", std::move(response)));
    auto str = response.string().unwrapOrDefault();
    if (str.empty() || str == "-1") {
        co_return Err(wrapError(response, "GD submit comment"));
    }

    co_return Ok();
}

Future<Result<web::UpdateProfilePayload>> Client::getGDCurrentProfileData() {
    auto response = co_await this->sendGDRequest(
        Form{}
            .param("targetAccountID", m_account.accountId)
            .param("gameVersion", "22")
            .param("binaryVersion", "45")
            .param("secret", "Wmfd2893gb7")
            .param("dvs", 3),
        "getGJUserInfo20.php"
    );

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
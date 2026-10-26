#pragma once
#include <Geode/Result.hpp>
#include <Geode/utils/web.hpp>
#include <matjson/std.hpp>
#include <asp/data/Cow.hpp>
#include <string>
#include <stdint.h>
#include <glaze/glaze.hpp>

using namespace asp::data;
using namespace geode::prelude;
using geode::utils::web::WebRequest;
using geode::utils::web::WebResponse;

namespace argon::web {

enum class ChallengeType {
    Comment,
    ProfileField,
};

inline std::string_view format_as(ChallengeType type) {
    switch (type) {
        case ChallengeType::Comment: return "comment";
        case ChallengeType::ProfileField: return "profile_field";
        default: return "unknown";
    }
}

struct ChallengeResponse {
    ChallengeType type;
    int32_t resource_id;
    uint32_t challenge_id;
    uint32_t secret;
};

struct VerifyResponse {
    bool completed;
    std::optional<std::string> refresh_token;
    std::optional<uint64_t> refresh_token_valid_until;
    std::optional<std::string> access_token;
    std::optional<uint64_t> access_token_valid_until;
};

struct RefreshResponse {
    std::string refresh_token;
    uint64_t refresh_token_valid_until;
    std::string access_token;
    uint64_t access_token_valid_until;
};

static CowString truncate(std::string_view s, size_t maxSize = 128) {
    if (s.size() >= maxSize) {
        // genius
        std::string out{s.substr(0, maxSize)};
        out.push_back('.');
        out.push_back('.');
        out.push_back('.');

        return CowString::fromOwned(std::move(out));
    } else {
        return CowString::fromBorrowed(s);
    }
}

static std::string wrapError(WebResponse& response, std::string_view what) {
    auto str = response.string().unwrapOrDefault();

    log::warn("(Argon) {} failed (code {})", what, response.code());
    log::warn("Response: '{}'", str);
    log::warn("Curl error message: '{}'", response.errorMessage());

    if (response.code() == -1) {
        // curl error, request did not even reach the server
        std::string fullmsg = std::move(str);
        auto emsg = response.errorMessage();
        if (!emsg.empty()) {
            if (fullmsg.empty()) {
                fullmsg = emsg;
            } else {
                fullmsg += fmt::format(" ({})", emsg);
            }
        }

        if (fullmsg.empty()) {
            fullmsg = "(unknown error, response and error buffer are empty)";
        }

        auto msg = truncate(fullmsg);

        return fmt::format("Request error ({}): {}", what, msg.toBorrowed());
    } else {
        // server error

        auto resp = str;
        if (resp.empty()) {
            resp = "(no response body)";
        }

        auto msg = truncate(resp);
        return fmt::format("Server error ({}, code {}): {}", what, response.code(), msg.toBorrowed());
    }
}

}

template <>
struct ::glz::meta<argon::web::ChallengeType> {
    using enum argon::web::ChallengeType;
    static constexpr auto value = glz::enumerate(
        "comment", Comment,
        "profile_field", ProfileField
    );
};

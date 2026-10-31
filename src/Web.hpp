#pragma once
#include <argon.hpp>
#include <Geode/utils/web.hpp>
#include "WebData.hpp"
#include "WebError.hpp"

namespace argon::web {

std::string getBaseServerUrl();

struct UpdateProfilePayload {
    int messages;
    int friendRequests;
    int commentHistory;
    std::string youtube;
    std::string twitter;
    std::string twitch;
    std::string instagram;
    std::string tiktok;
    std::string discord;
    std::string custom;
};

struct Form {
    Form() = default;
    Form(Form&&) noexcept = default;
    Form& operator=(Form&&) noexcept = default;

    StringBuffer<> buf;

    Form& param(std::string_view key, std::string_view value) & {
        if (buf.size() > 0) {
            buf.append('&');
        }

        buf.append(key);
        buf.append('=');
        buf.append(value);
        return *this;
    }

    Form&& param(std::string_view key, std::string_view value) && {
        return std::move(this->param(key, value));
    }

    template <std::integral T>
    Form& param(std::string_view key, T value) & {
        return param(key, fmt::to_string(value));
    }

    template <std::integral T>
    Form&& param(std::string_view key, T value) && {
        return std::move(this->param(key, fmt::to_string(value)));
    }

    std::string str() {
        return buf.str();
    }
};

struct Client {
    AccountData m_account;
    std::string m_serverUrl;
    Mod* m_reqMod;
    bool m_certVerification = true;
    std::string m_refreshToken;

    arc::Future<ArgonApiResult<ChallengeResponse>> sendLogin(std::span<const ChallengeType> supported);
    arc::Future<ArgonApiResult<VerifyResponse>> sendVerify(uint32_t challengeId, uint32_t answer);
    arc::Future<ArgonApiResult<RefreshResponse>> sendRefresh();
    arc::Future<ArgonApiResult<WebTokenResponse>> sendGenerateWebToken();

    arc::Future<Result<>> sendGDUpdateProfile(UpdateProfilePayload payload);
    arc::Future<Result<>> sendGDSubmitComment(int32_t levelId, std::string content);
    arc::Future<Result<UpdateProfilePayload>> getGDCurrentProfileData();

private:
    static std::string getUserAgent();

    template <typename R, typename T>
    arc::Future<ArgonApiResult<R>> sendArgonRequest(T&& payload, std::string_view suffix) {
        ARC_FRAME();
        using Payload = std::decay_t<T>;

        auto req = WebRequest()
            .userAgent(getUserAgent())
            .certVerification(m_certVerification)
            .timeout(std::chrono::seconds(10));

        if constexpr (!std::is_void_v<Payload>) {
            req.bodyString(glz::write_json(payload).value());
            req.header("Content-Type", "application/json");
        }

        if (!m_refreshToken.empty()) {
            req.header("Authorization", fmt::format("Bearer {}", m_refreshToken));
        }

        std::string url = fmt::format("{}/{}", m_serverUrl, suffix);
        auto response = co_await req.post(std::move(url));
        if (response.ok()) {
            if constexpr (!std::is_void_v<R>) {
                auto s = glz::read_json<R>(response.string().unwrapOrDefault());
                if (s) {
                    co_return Ok(std::move(s).value());
                } else {
                    co_return Err(ArgonApiError::custom("Failed to parse server response"));
                }
            } else {
                co_return Ok();
            }
        } else {
            struct ErrorBody {
                std::string error;
                std::string code;
            };

            auto resp = response.string().unwrapOrDefault();
            auto s = glz::read_json<ErrorBody>(resp);
            if (!s) {
                co_return Err(ArgonApiError::custom(fmt::format("Failed to parse server error response (code {}): '{}'", response.code(), resp)));
            }

            co_return Err(ArgonApiError::fromArgonError(std::move(s->code), std::move(s->error)));
        }
    }

    arc::Future<utils::web::WebResponse> sendGDRequest(
        std::optional<Form> payload,
        std::string_view suffix
    );
};

}

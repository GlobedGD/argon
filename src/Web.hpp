#pragma once
#include <argon.hpp>
#include <Geode/utils/web.hpp>
#include "WebData.hpp"
#include "WebError.hpp"

namespace argon::web {

std::string getBaseServerUrl();

enum class Method {
    Get,
    Post,
    Put,
    Delete,
};

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

template <typename T, auto Opts = glz::opts{.error_on_unknown_keys = false}>
ArgonApiResult<T> parseJson(std::string_view str) {
    T val{};
    glz::error_ctx err = glz::read<Opts, T>(val, str);
    if (err) {
        auto& msg = err.custom_error_message;
        asp::BoxedString str;
        if (msg.empty()) {
            str = fmt::format("Failed to parse JSON: json error {}", (int)err.ec);
        } else {
            str = fmt::format("Failed to parse JSON: {}", msg);
        }

        return Err(ArgonApiError::custom(std::move(str)));
    }

    return Ok(std::move(val));
}

struct Client {
    AccountData m_account;
    std::string m_serverUrl;
    Mod* m_reqMod;
    bool m_certVerification = true;
    std::string m_refreshToken;

    Client(AccountData account, std::string serverUrl, Mod* reqMod, bool certVerification)
        : m_account(std::move(account)),
          m_serverUrl(std::move(serverUrl)),
          m_reqMod(reqMod),
          m_certVerification(certVerification) {}

    Client(const Client&) = default;
    Client& operator=(const Client&) = default;
    Client(Client&&) noexcept = default;
    Client& operator=(Client&&) noexcept = default;

    Future<ArgonApiResult<ChallengeResponse>> sendLogin(std::span<const ChallengeType> supported);
    Future<ArgonApiResult<VerifyResponse>> sendVerify(uint32_t challengeId, uint32_t answer);
    Future<ArgonApiResult<RefreshResponse>> sendRefresh();
    Future<ArgonApiResult<WebTokenResponse>> sendGenerateWebToken();

    // discord
    Future<ArgonApiResult<DiscordLinkStateResponse>> sendGetDiscordState();
    Future<ArgonApiResult<Oauth2RedirectResponse>> sendDiscordLink();
    Future<ArgonApiResult<Oauth2RedirectResponse>> sendDiscordSync();
    Future<ArgonApiResult<void>> sendDiscordUnlink();

    // gd requests
    Future<Result<>> sendGDUpdateProfile(UpdateProfilePayload payload);
    Future<Result<int32_t>> sendGDSubmitComment(int32_t levelId, std::string content);
    Future<Result<>> sendGDDeleteComment(int32_t levelId, int32_t commentId);
    Future<Result<UpdateProfilePayload>> getGDCurrentProfileData();

private:
    static std::string getUserAgent();

    template <typename R, Method M = Method::Post, typename T = std::monostate>
    Future<ArgonApiResult<R>> sendArgonRequest(std::string_view suffix, T&& payload = std::monostate{}) {
        ARC_FRAME();
        using Payload = std::decay_t<T>;

        auto req = WebRequest()
            .userAgent(getUserAgent())
            .certVerification(m_certVerification)
            .timeout(asp::Duration::fromSecs(10));

        if constexpr (!std::is_same_v<Payload, std::monostate>) {
            req.bodyString(glz::write_json(payload).value());
            req.header("Content-Type", "application/json");
        }

        if (!m_refreshToken.empty()) {
            req.header("Authorization", fmt::format("Bearer {}", m_refreshToken));
        }

        std::string url = fmt::format("{}/{}", m_serverUrl, suffix);
        std::string method;
        switch (M) {
            case Method::Get: method = "GET"; break;
            case Method::Post: method = "POST"; break;
            case Method::Put: method = "PUT"; break;
            case Method::Delete: method = "DELETE"; break;
            default: method = "POST"; break;
        }

        auto response = co_await req.send(std::move(method), std::move(url));
        if (response.ok()) {
            if constexpr (!std::is_void_v<R>) {
                co_return parseJson<R>(response.string().unwrapOrDefault());
            } else {
                co_return Ok();
            }
        } else {
            struct ErrorBody {
                std::string error;
                std::string code;
            };

            auto raw = response.string().unwrapOrDefault();
            auto parseResult = parseJson<ErrorBody>(raw);
            if (!parseResult) {
                log::warn("Request to '{}' failed and error could not be parsed, code {}, body: {}", suffix, response.code(), raw);
                co_return Err(ArgonApiError::custom(fmt::format(
                    "Server returned code {} and parsing error response failed: {}", response.code(), parseResult.unwrapErr()
                )));
            }

            auto body = std::move(parseResult).unwrap();
            co_return Err(ArgonApiError::fromArgonError(std::move(body.code), std::move(body.error)));
        }
    }

    Future<utils::web::WebResponse> sendGDRequest(
        std::optional<Form> payload,
        std::string_view suffix
    );
};

}

#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <asp/ptr/BoxedString.hpp>

namespace argon::web {

class ArgonApiError {
public:
    static ArgonApiError fromArgonError(asp::BoxedString errorCode, asp::BoxedString message) {
        ArgonApiError err;
        err.m_errorCode = std::move(errorCode);
        err.m_message = std::move(message);
        return err;
    }

    static ArgonApiError custom(asp::BoxedString message) {
        ArgonApiError err;
        err.m_message = std::move(message);
        return err;
    }

    std::optional<std::string_view> getArgonErrorCode() {
        return m_errorCode.empty() ? std::nullopt : std::optional<std::string_view>(m_errorCode);
    }

    std::string toString() const {
        return std::string{m_message};
    }

    operator std::string() const {
        return this->toString();
    }

private:
    asp::BoxedString m_errorCode;
    asp::BoxedString m_message;
};

template <typename T>
using ArgonApiResult = geode::Result<T, ArgonApiError>;

}

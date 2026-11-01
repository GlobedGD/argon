#pragma once

#include <Geode/Result.hpp>
#include <cocos2d.h>
#include <utility>
#include <mutex>

namespace argon {

template <typename Derived>
class SingletonBase {
public:
    // no copy
    SingletonBase(const SingletonBase&) = delete;
    SingletonBase& operator=(const SingletonBase&) = delete;
    // no move
    SingletonBase(SingletonBase&&) = delete;
    SingletonBase& operator=(SingletonBase&&) = delete;

    static Derived& get() {
        static Derived instance;

        return instance;
    }

protected:
    SingletonBase() {}
};

// ccobject that stores a custom struct
template <typename T>
class CCData : public cocos2d::CCObject {
    T inner;

    CCData(T data) : inner(std::move(data)) {}

    template <typename... Args>
    CCData(Args&&... args) : inner(std::forward<Args>(args)...) {}

public:
    template <typename... Args>
    static CCData* create(Args&&... args) {
        auto ret = new CCData{std::forward<Args>(args)...};
        ret->autorelease();
        return ret;
    }

    T& data() {
        return inner;
    }

    T* operator->() const {
        return &inner;
    }

    T& operator*() const {
        return inner;
    }
};

using CCMutex = CCData<std::mutex>;

template <typename T, typename E>
inline geode::Result<T> mapPrefix(geode::Result<T, E> result, std::string_view prefix) {
    if (result) {
        return result;
    } else {
        return geode::Err(fmt::format("{}: {}", prefix, result.unwrapErr()));
    }
}

}


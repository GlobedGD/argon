#pragma once
#include <Geode/Result.hpp>
#include <vector>
#include <array>
#include <span>

namespace argon {

struct Key {
    std::array<uint8_t, 32> data;
};

Key createKey();

std::vector<uint8_t> encrypt(std::span<const uint8_t> data, const Key& key);
geode::Result<std::vector<uint8_t>> decrypt(std::span<const uint8_t> data, const Key& key);

}

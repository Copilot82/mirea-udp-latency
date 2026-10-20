#pragma once
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace app {
inline std::uint64_t Number(std::string_view text, std::uint64_t min, std::uint64_t max) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < min || value > max)
        throw std::invalid_argument("numeric argument outside allowed range: " + std::string(text));
    return value;
}
inline bool ValidId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
}

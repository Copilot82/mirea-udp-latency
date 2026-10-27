#pragma once
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace app {
struct Impairment { bool drop; std::uint64_t delayUs; };
// mt19937 and rejection sampling make seeded decisions identical across stdlibs.
class Emulator {
public:
    Emulator(std::string profile, std::uint32_t seed)
        : profile_(std::move(profile)), loss_(seed), delay_(seed ^ 0x9e3779b9U) {
        if (profile_ != "baseline" && profile_ != "delay_50" && profile_ != "delay_100" &&
            profile_ != "jitter" && profile_ != "loss_5" && profile_ != "combined" &&
            profile_ != "loss_10" && profile_ != "loss_20" &&
            profile_ != "delay_100_loss_5" && profile_ != "jitter_loss_10")
            throw std::invalid_argument("unknown emulator profile");
    }
    Impairment Next() {
        unsigned loss = 0;
        if (profile_ == "loss_5" || profile_ == "combined" || profile_ == "delay_100_loss_5") loss = 500;
        if (profile_ == "loss_10" || profile_ == "jitter_loss_10") loss = 1000;
        if (profile_ == "loss_20") loss = 2000;
        const bool drop = Draw(loss_, 10000) < loss;
        std::uint64_t ms = 0;
        if (profile_ == "delay_50") ms = 50;
        if (profile_ == "delay_100" || profile_ == "delay_100_loss_5") ms = 100;
        if (profile_ == "jitter" || profile_ == "combined" || profile_ == "jitter_loss_10") ms = 50 + Draw(delay_, 101);
        return {drop, ms * 1000};
    }
private:
    static std::uint32_t Draw(std::mt19937& rng, std::uint32_t bound) {
        const std::uint64_t span = std::uint64_t{1} << 32;
        const auto limit = span - span % bound;
        std::uint32_t value;
        do { value = static_cast<std::uint32_t>(rng()); } while (value >= limit);
        return value % bound;
    }
    std::string profile_;
    std::mt19937 loss_, delay_;
};
}

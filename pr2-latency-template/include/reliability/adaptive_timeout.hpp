#pragma once
#include "telemetry/telemetry.hpp"
#include <cstdint>

class AdaptiveTimeout {
public:
    void OnSample(double rttMs);
    double RtoMs() const;
    std::uint64_t RtoUs() const;
    double SrttMs() const { return estimator_.SrttMs(); }
    double RttVarMs() const { return rttVarMs_; }
private:
    RttEstimator estimator_;
    double rttVarMs_ = 0;
};

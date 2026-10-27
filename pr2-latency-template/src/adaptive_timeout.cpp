#include "reliability/adaptive_timeout.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

void AdaptiveTimeout::OnSample(double rttMs) {
    if (!std::isfinite(rttMs) || rttMs < 0 || rttMs > 60000)
        throw std::invalid_argument("RTT outside 0..60000 ms");
    // RTTVAR вычисляется по предыдущему SRTT, затем обновляется SRTT.
    rttVarMs_ = estimator_.Initialized()
        ? 0.75 * rttVarMs_ + 0.25 * std::abs(estimator_.SrttMs() - rttMs)
        : rttMs / 2.0;
    estimator_.OnSample(rttMs);
}
double AdaptiveTimeout::RtoMs() const {
    return estimator_.Initialized()
        ? std::clamp(estimator_.SrttMs() + 4.0 * rttVarMs_, 100.0, 3000.0) : 1000.0;
}
std::uint64_t AdaptiveTimeout::RtoUs() const {
    return static_cast<std::uint64_t>(std::ceil(RtoMs() * 1000.0));
}

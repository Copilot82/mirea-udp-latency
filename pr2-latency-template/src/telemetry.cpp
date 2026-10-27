#include "telemetry/telemetry.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

const char* StatusName(ResponseStatus status) {
    switch (status) {
        case ResponseStatus::Pending: return "pending";
        case ResponseStatus::Received: return "received";
        case ResponseStatus::Timeout: return "timeout";
        case ResponseStatus::Late: return "late_response";
        case ResponseStatus::Duplicate: return "duplicate_response";
        case ResponseStatus::Unknown: return "unknown_response";
        case ResponseStatus::Invalid: return "invalid_response";
        case ResponseStatus::Foreign: return "foreign_response";
    }
    return "invalid_response";
}

Telemetry::Telemetry(std::uint64_t timeoutUs, std::size_t maxRecords, std::string experimentId)
    : timeoutUs_(timeoutUs), maxRecords_(maxRecords), experimentId_(std::move(experimentId)) {
    if (!timeoutUs_ || !maxRecords_ || maxRecords_ > 65536 || experimentId_.empty())
        throw std::invalid_argument("invalid telemetry configuration");
}

void Telemetry::OnPingSent(std::uint16_t seq, std::uint64_t nowUs) {
    if (lastSentUs_ && nowUs <= *lastSentUs_)
        throw std::invalid_argument("send timestamps must increase strictly");
    const auto found = records_.find(seq);
    if (found != records_.end()) {
        if (found->second.status == ResponseStatus::Pending)
            throw std::logic_error("sequence still pending");
        if (nowUs <= found->second.sentAtUs)
            throw std::invalid_argument("reused sequence needs a new timestamp");
        records_.erase(found);
        order_.erase(std::find(order_.begin(), order_.end(), seq));
    }
    if (records_.size() == maxRecords_) {
        const auto victim = std::find_if(order_.begin(), order_.end(), [this](auto s) {
            return records_.at(s).status != ResponseStatus::Pending;
        });
        if (victim == order_.end()) throw std::length_error("all flight records are pending");
        records_.erase(*victim);
        order_.erase(victim);
    }
    FlightRecord record;
    record.sequenceNumber = seq;
    record.sentAtUs = nowUs;
    record.experimentId = experimentId_;
    record.sample = ++totals_.sent;
    records_.emplace(seq, std::move(record));
    order_.push_back(seq);
    lastSentUs_ = nowUs;
}

const FlightRecord* Telemetry::FindRecord(std::uint16_t seq) const {
    const auto it = records_.find(seq);
    return it == records_.end() ? nullptr : &it->second;
}

void Telemetry::Emit(ResponseStatus status, const FlightRecord* r,
                     std::uint16_t seq, std::uint64_t nowUs) {
    // Only successful terminal rows have RTT/SRTT; diagnostic rows cannot be
    // accidentally included in an aggregate by filtering for numeric RTT.
    const bool received = status == ResponseStatus::Received;
    events_.push_back({status, r ? r->sample : 0, seq,
        r ? std::optional<std::uint64_t>(r->sentAtUs) : std::nullopt, nowUs,
        received ? r->rttMs : std::nullopt, received ? r->srttMs : std::nullopt});
}

void Telemetry::Timeout(FlightRecord& r, std::uint64_t nowUs) {
    r.status = ResponseStatus::Timeout;
    ++totals_.timeouts;
    Emit(ResponseStatus::Timeout, &r, r.sequenceNumber, nowUs);
}

ResponseStatus Telemetry::OnPong(std::uint16_t seq, std::uint64_t echo, std::uint64_t nowUs) {
    const auto it = records_.find(seq);
    if (it == records_.end()) {
        ++totals_.unknown;
        Emit(ResponseStatus::Unknown, nullptr, seq, nowUs);
        return ResponseStatus::Unknown;
    }
    auto& r = it->second;
    if (echo != r.sentAtUs || nowUs < r.sentAtUs) {
        ++totals_.invalid;
        Emit(ResponseStatus::Invalid, &r, seq, nowUs);
        return ResponseStatus::Invalid;
    }
    if (r.status == ResponseStatus::Received || r.status == ResponseStatus::Late) {
        ++totals_.duplicates;
        Emit(ResponseStatus::Duplicate, &r, seq, nowUs);
        return ResponseStatus::Duplicate;
    }
    if (r.status == ResponseStatus::Pending && nowUs - r.sentAtUs >= timeoutUs_)
        Timeout(r, nowUs);
    if (r.status == ResponseStatus::Timeout) {
        r.status = ResponseStatus::Late;
        ++totals_.late;
        Emit(ResponseStatus::Late, &r, seq, nowUs);
        return ResponseStatus::Late;
    }
    const double rtt = double(nowUs - r.sentAtUs) / 1000.0;
    if (!rtts_.empty()) jitterSumMs_ += std::abs(rtt - rtts_.back());
    estimator_.OnSample(rtt);
    rtts_.push_back(rtt);
    ++totals_.received;
    r.status = ResponseStatus::Received;
    r.rttMs = rtt;
    r.srttMs = estimator_.SrttMs();
    Emit(ResponseStatus::Received, &r, seq, nowUs);
    return ResponseStatus::Received;
}

ResponseStatus Telemetry::OnPong(std::uint16_t seq, std::uint64_t nowUs) {
    const auto* r = FindRecord(seq);
    return OnPong(seq, r ? r->sentAtUs : 0, nowUs);
}

std::size_t Telemetry::ExpireOverdue(std::uint64_t nowUs) {
    std::size_t count = 0;
    for (const auto seq : order_) {
        auto& r = records_.at(seq);
        if (r.status == ResponseStatus::Pending && nowUs >= r.sentAtUs &&
            nowUs - r.sentAtUs >= timeoutUs_) {
            Timeout(r, nowUs);
            ++count;
        }
    }
    return count;
}

std::vector<MeasurementEvent> Telemetry::DrainEvents() {
    std::vector<MeasurementEvent> result;
    result.swap(events_);
    return result;
}

double Telemetry::MeanJitterMs() const {
    return rtts_.size() < 2 ? 0 : jitterSumMs_ / double(rtts_.size() - 1);
}

TelemetrySummary Telemetry::Summary() const {
    auto s = totals_;
    s.jitterMs = MeanJitterMs();
    s.lossPercent = s.sent ? 100.0 * double(s.timeouts) / double(s.sent) : 0;
    if (!rtts_.empty()) {
        auto sorted = rtts_;
        std::sort(sorted.begin(), sorted.end());
        s.minMs = sorted.front();
        s.maxMs = sorted.back();
        s.meanMs = std::accumulate(sorted.begin(), sorted.end(), 0.0) / double(sorted.size());
        const auto n = sorted.size();
        s.medianMs = n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
        s.srttMs = estimator_.SrttMs();
    }
    return s;
}

void RttEstimator::OnSample(double rttMs) {
    if (!std::isfinite(rttMs) || rttMs < 0)
        throw std::invalid_argument("RTT must be finite and nonnegative");
    srttMs_ = initialized_ ? 0.875 * srttMs_ + 0.125 * rttMs : rttMs;
    initialized_ = true;
}
